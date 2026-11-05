#include "DeviceServices.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace device {
namespace {
constexpr std::size_t maximumBody = 4096;
constexpr char storageNamespace[] = "catfeeder";
extern const char pageStart[] asm("_binary_index_html_start");
extern const char pageEnd[] asm("_binary_index_html_end");
SemaphoreHandle_t stateMutex = nullptr;
nvs_handle_t storage = 0;
catfeeder::Settings savedSettings = catfeeder::demoSettings();
bool pendingSettings = false;
bool settingsValid = true;
bool statusRecognized = false;
catfeeder::State statusAccess = catfeeder::State::Idle;
catfeeder::State statusDispensing = catfeeder::State::Idle;
std::uint32_t statusTime = 0;
char wifiName[32] = {};
char wifiPassword[13] = {};

class StateLock {
public:
    StateLock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
    ~StateLock() { xSemaphoreGive(stateMutex); }
};

const char* stateName(catfeeder::State state) {
    switch (state) {
    case catfeeder::State::Idle: return "idle";
    case catfeeder::State::Running: return "running";
    case catfeeder::State::Complete: return "complete";
    case catfeeder::State::Fault: return "fault";
    }
    return "unknown";
}

bool integer(const cJSON* value, double minimum, double maximum) {
    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
           value->valuedouble >= minimum && value->valuedouble <= maximum &&
           std::floor(value->valuedouble) == value->valuedouble;
}

bool parseSettings(const char* data, catfeeder::Settings& settings, std::string& error,
                   bool readingStoredRecord = false) {
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithOpts(data, &end, true);
    if (!root) { error = "Send a valid JSON settings object."; return false; }
    const auto schema = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    const auto revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
    const auto portion = cJSON_GetObjectItemCaseSensitive(root, "portionGrams");
    const auto interval = cJSON_GetObjectItemCaseSensitive(root, "minimumFeedIntervalSeconds");
    const auto times = cJSON_GetObjectItemCaseSensitive(root, "feedingTimes");
    const auto cats = cJSON_GetObjectItemCaseSensitive(root, "cats");
    bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 6 &&
                 integer(schema, 1, 1) && integer(revision, 0, 4294967294.0) &&
                 cJSON_IsNumber(portion) && integer(interval, 1, 86400) &&
                 cJSON_IsArray(times) && cJSON_GetArraySize(times) <= 6 &&
                 cJSON_IsArray(cats) && cJSON_GetArraySize(cats) <= (readingStoredRecord ? 8 : 1);
    if (valid) {
        settings.revision = static_cast<std::uint32_t>(revision->valuedouble);
        settings.portionGrams = portion->valuedouble;
        settings.minimumFeedIntervalSeconds = static_cast<std::uint32_t>(interval->valuedouble);
        settings.feedingTimes.clear();
        settings.cats.clear();
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, times) {
            if (!cJSON_IsString(item)) { valid = false; break; }
            settings.feedingTimes.push_back(item->valuestring);
        }
        cJSON_ArrayForEach(item, cats) {
            const auto name = cJSON_GetObjectItemCaseSensitive(item, "name");
            const auto tag = cJSON_GetObjectItemCaseSensitive(item, "tag");
            if (!cJSON_IsObject(item) || cJSON_GetArraySize(item) != 2 ||
                !cJSON_IsString(name) || !cJSON_IsString(tag)) { valid = false; break; }
            settings.cats.push_back({name->valuestring, tag->valuestring});
        }
    }
    cJSON_Delete(root);
    if (!valid) { error = "Settings contain missing, unexpected, or invalid fields."; return false; }
    if (readingStoredRecord && settings.cats.size() > 1) {
        // Preserve the first profile and all feeding settings from the early UI.
        // The stored record is replaced only when the user explicitly saves again.
        settings.cats.resize(1);
        std::puts("Loaded first cat profile from the earlier multi-cat configuration.");
    }
    return catfeeder::validateSettings(settings, error);
}

cJSON* settingsJson(const catfeeder::Settings& settings) {
    auto root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON_AddNumberToObject(root, "revision", settings.revision);
    cJSON_AddNumberToObject(root, "portionGrams", settings.portionGrams);
    cJSON_AddNumberToObject(root, "minimumFeedIntervalSeconds", settings.minimumFeedIntervalSeconds);
    auto times = cJSON_AddArrayToObject(root, "feedingTimes");
    for (const auto& time : settings.feedingTimes) cJSON_AddItemToArray(times, cJSON_CreateString(time.c_str()));
    auto cats = cJSON_AddArrayToObject(root, "cats");
    for (const auto& cat : settings.cats) {
        auto item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", cat.name.c_str());
        cJSON_AddStringToObject(item, "tag", cat.tag.c_str());
        cJSON_AddItemToArray(cats, item);
    }
    return root;
}

void headers(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}

esp_err_t sendJson(httpd_req_t* request, cJSON* root) {
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    headers(request);
    httpd_resp_set_type(request, "application/json");
    const auto result = httpd_resp_sendstr(request, body);
    cJSON_free(body);
    return result;
}

esp_err_t errorJson(httpd_req_t* request, const char* status, const char* error) {
    httpd_resp_set_status(request, status);
    auto root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", error);
    return sendJson(request, root);
}

esp_err_t servePage(httpd_req_t* request) {
    headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, pageStart, pageEnd - pageStart - 1);
}

esp_err_t getSettings(httpd_req_t* request) {
    return sendJson(request, settingsJson(currentSettings()));
}

esp_err_t getStatus(httpd_req_t* request) {
    auto root = cJSON_CreateObject();
    {
        StateLock lock;
        cJSON_AddStringToObject(root, "access", stateName(statusAccess));
        cJSON_AddStringToObject(root, "dispensing", stateName(statusDispensing));
        cJSON_AddBoolToObject(root, "recognized", statusRecognized);
        cJSON_AddBoolToObject(root, "pendingSettings", pendingSettings);
        cJSON_AddBoolToObject(root, "settingsValid", settingsValid);
        cJSON_AddNumberToObject(root, "uptimeSeconds", statusTime / 1000);
        cJSON_AddNumberToObject(root, "revision", savedSettings.revision);
    }
    cJSON_AddStringToObject(root, "ssid", wifiName);
    cJSON_AddStringToObject(root, "address", "192.168.4.1");
    cJSON_AddBoolToObject(root, "demo", true);
    cJSON_AddBoolToObject(root, "scheduleActive", false);
    cJSON_AddBoolToObject(root, "portionCalibrated", false);
    cJSON_AddNumberToObject(root, "freeHeapBytes", esp_get_free_heap_size());
    return sendJson(request, root);
}

esp_err_t saveSettings(httpd_req_t* request) {
    char contentType[48] = {};
    char marker[32] = {};
    httpd_req_get_hdr_value_str(request, "Content-Type", contentType, sizeof(contentType));
    httpd_req_get_hdr_value_str(request, "X-CatFeeder-Request", marker, sizeof(marker));
    if (std::strncmp(contentType, "application/json", 16) != 0 ||
        std::strcmp(marker, "configuration") != 0) {
        return errorJson(request, "415 Unsupported Media Type", "Use the configuration page to save JSON settings.");
    }
    if (request->content_len == 0 || request->content_len > maximumBody) {
        return errorJson(request, "413 Payload Too Large", "Settings must fit within 4096 bytes.");
    }
    std::string body(request->content_len, '\0');
    std::size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(request, &body[received], body.size() - received);
        if (count <= 0) return errorJson(request, "408 Request Timeout", "Settings upload did not complete.");
        received += static_cast<std::size_t>(count);
    }
    if (body.find('\0') != std::string::npos) {
        return errorJson(request, "400 Bad Request", "Settings contain an invalid null byte.");
    }
    catfeeder::Settings next;
    std::string error;
    if (!parseSettings(body.c_str(), next, error)) {
        return errorJson(request, "400 Bad Request", error.c_str());
    }
    bool conflict = false;
    {
        StateLock lock;
        conflict = next.revision != savedSettings.revision;
    }
    if (conflict) return errorJson(request, "409 Conflict", "Settings changed in another session. Reload before saving.");
    ++next.revision;
    auto root = settingsJson(next);
    char* serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!serialized) return errorJson(request, "500 Internal Server Error", "Not enough memory to save settings.");
    // The HTTP task is the only writer. Never hold the state mutex during flash I/O.
    esp_err_t result = nvs_set_str(storage, "settings", serialized);
    if (result == ESP_OK) result = nvs_commit(storage);
    cJSON_free(serialized);
    if (result != ESP_OK) return errorJson(request, "500 Internal Server Error", "Could not commit settings to device storage.");
    {
        StateLock lock;
        savedSettings = next;
        pendingSettings = true;
        settingsValid = true;
    }
    auto response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "saved", true);
    cJSON_AddNumberToObject(response, "revision", next.revision);
    return sendJson(request, response);
}

esp_err_t loadStorage() {
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result; // Do not erase existing data on an init error.
    result = nvs_open(storageNamespace, NVS_READWRITE, &storage);
    if (result != ESP_OK) return result;
    std::size_t size = 0;
    result = nvs_get_str(storage, "settings", nullptr, &size);
    if (result == ESP_OK && size > 0 && size <= maximumBody + 1) {
        std::string data(size, '\0');
        std::string error;
        result = nvs_get_str(storage, "settings", &data[0], &size);
        settingsValid = result == ESP_OK && parseSettings(data.c_str(), savedSettings, error, true);
    } else if (result != ESP_ERR_NVS_NOT_FOUND) {
        settingsValid = false;
    }
    if (!settingsValid) {
        savedSettings = catfeeder::Settings{}; // Corrupt settings never authorize the demo cat.
        std::puts("Stored configuration could not be read. Save valid settings through the web page.");
    }
    size = sizeof(wifiPassword);
    result = nvs_get_str(storage, "ap_password", wifiPassword, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        for (unsigned index = 0; index < sizeof(wifiPassword) - 1; ++index) {
            wifiPassword[index] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
        }
        result = nvs_set_str(storage, "ap_password", wifiPassword);
        if (result == ESP_OK) result = nvs_commit(storage);
    }
    if (result != ESP_OK || std::strlen(wifiPassword) != 12) return ESP_FAIL;
    return ESP_OK;
}
} // namespace

esp_err_t startServices() {
    stateMutex = xSemaphoreCreateMutex();
    if (!stateMutex) return ESP_ERR_NO_MEM;
    esp_err_t result = loadStorage();
    if (result != ESP_OK) return result;
    result = esp_netif_init();
    if (result != ESP_OK) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;
    auto ap = esp_netif_create_default_wifi_ap();
    if (!ap) return ESP_FAIL;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) return result;
    // Keep Wi-Fi mode/config in RAM; application settings have their own NVS record.
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    std::uint8_t mac[6] = {};
    result = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (result != ESP_OK) return result;
    std::snprintf(wifiName, sizeof(wifiName), "CatFeeder-%02X%02X", mac[4], mac[5]);
    wifi_config_t wifi = {};
    std::memcpy(wifi.ap.ssid, wifiName, std::strlen(wifiName));
    wifi.ap.ssid_len = std::strlen(wifiName);
    std::memcpy(wifi.ap.password, wifiPassword, std::strlen(wifiPassword));
    wifi.ap.channel = 1;
    wifi.ap.max_connection = 4;
    wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result != ESP_OK) return result;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    httpd_handle_t server = nullptr;
    result = httpd_start(&server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = servePage, .user_ctx = nullptr },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = getSettings, .user_ctx = nullptr },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = saveSettings, .user_ctx = nullptr },
        { .uri = "/api/status", .method = HTTP_GET, .handler = getStatus, .user_ctx = nullptr },
    };
    for (const auto& route : routes) {
        result = httpd_register_uri_handler(server, &route);
        if (result != ESP_OK) return result;
    }
    std::printf("Configuration network: %s. Send w for password and address.\n", wifiName);
    return ESP_OK;
}

catfeeder::Settings currentSettings() {
    if (!stateMutex) return catfeeder::Settings{};
    StateLock lock;
    return savedSettings;
}

bool takePendingSettings(catfeeder::Settings& settings) {
    if (!stateMutex) return false;
    StateLock lock;
    if (!pendingSettings) return false;
    settings = savedSettings;
    pendingSettings = false;
    return true;
}

void publishStatus(catfeeder::State access, catfeeder::State dispensing,
                   bool recognized, std::uint32_t now) {
    if (!stateMutex) return;
    StateLock lock;
    statusAccess = access;
    statusDispensing = dispensing;
    statusRecognized = recognized;
    statusTime = now;
}

void printWifiDetails() {
    std::printf("Wi-Fi network: %s\nWi-Fi password: %s\nOpen http://192.168.4.1\n", wifiName, wifiPassword);
}
} // namespace device
