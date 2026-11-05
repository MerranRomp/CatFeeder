#include <cstdio>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DeviceServices.h"
#endif

#include "Fakes.h"

namespace {
std::uint32_t nowMs() {
#ifdef ARDUINO
    return millis();
#else
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
#endif
}

void printLine(const char* text) {
#ifdef ARDUINO
    Serial.println(text);
#else
    std::puts(text);
#endif
}

int readCommand() {
#ifdef ARDUINO
    return Serial.available() > 0 ? Serial.read() : -1;
#else
    unsigned char command = 0;
    // Zero timeout: an absent serial command must never stall controller updates.
    return uart_read_bytes(UART_NUM_0, &command, 1, 0) == 1 ? command : -1;
#endif
}

simulation::MockAccess access;
simulation::MockDispenser dispenser;
simulation::MemoryHistory history; // Demo only: lost when the ESP32 resets.
bool mockCatPresent = false;
#ifndef ARDUINO
bool servicesHealthy = false;
catfeeder::Settings activeSettings;

catfeeder::Config settingsConfig(const catfeeder::Settings& settings) {
    auto config = simulation::mockConfig();
    config.accessTimeoutMs = 10000;
    config.dispenseTimeoutMs = 10000;
    config.minimumFeedIntervalMs = settings.minimumFeedIntervalSeconds * 1000;
    config.authorizedIds.clear();
    for (const auto& cat : settings.cats) config.authorizedIds.push_back(cat.tag);
    // Grams are a saved target only. Mock units remain independent of calibration.
    return config;
}
#endif

catfeeder::Config demoConfig() {
#ifndef ARDUINO
    return servicesHealthy ? settingsConfig(activeSettings) : catfeeder::Config{};
#else
    auto config = simulation::mockConfig();
    // Give a person time to send a completion command in the serial monitor.
    config.accessTimeoutMs = 10000;
    config.dispenseTimeoutMs = 10000;
    return config;
#endif
}

bool recognizedCat() {
    if (!mockCatPresent) return false;
#ifndef ARDUINO
    for (const auto& cat : activeSettings.cats) {
        if (cat.tag == "mock-cat") return true;
    }
    return false;
#else
    return true;
#endif
}

catfeeder::Controller& controller() {
    // First called from the firmware entry point, after runtime initialization.
    static catfeeder::Controller instance(demoConfig(), access, dispenser, history, nowMs());
    return instance;
}

catfeeder::Inputs readMockInputs(std::uint32_t now) {
    auto inputs = simulation::mockInputs(now);
    if (!mockCatPresent) {
        inputs.identification = catfeeder::IdentificationStatus::NoIdentifier;
        inputs.identifier.clear();
    }
    return inputs; // Safety fields are mocked, not physical sensor readings.
}

const char* stateName(catfeeder::State state) {
    switch (state) {
    case catfeeder::State::Idle: return "idle";
    case catfeeder::State::Running: return "running";
    case catfeeder::State::Complete: return "complete";
    case catfeeder::State::Fault: return "fault";
    }
    return "unknown";
}

void printStatus() {
    char message[128];
    std::snprintf(message, sizeof(message), "Mock cat: %s | access: %s | dispenser: %s",
                  recognizedCat() ? "recognized" : (mockCatPresent ? "unauthorized" : "absent"),
                  stateName(controller().accessState()),
                  stateName(controller().dispenseState()));
    printLine(message);
}

void printHelp() {
    printLine("MOCK DEMO ONLY: no real readers, actuators, or safety sensors.");
    printLine("i = mock cat present, u = mock cat absent");
    printLine("a = request access, d = request one mock portion");
    printLine("c = confirm mock operation complete, s = status, h = help");
    printLine("Complete operations within 10s. The initial mock feeding interval is 5s.");
    printLine("Faults stay latched. Reset restarts this RAM-only demo.");
#ifndef ARDUINO
    printLine("w = show configuration Wi-Fi network, password, and page address");
#endif
}

void handleCommand(char command, std::uint32_t now) {
    switch (command) {
    case 'i':
        mockCatPresent = true;
        break;
    case 'u':
        mockCatPresent = false;
        break;
    case 'a':
        access.reported = catfeeder::OperationStatus::Running;
        printLine(controller().requestAccess(now, readMockInputs(now))
                      ? "Mock access accepted." : "Mock access denied.");
        break;
    case 'd':
        dispenser.reported = catfeeder::OperationStatus::Running;
        printLine(controller().requestDispense(now, readMockInputs(now))
                      ? "Mock dispensing accepted." : "Mock dispensing denied.");
        break;
    case 'c':
        if (controller().accessState() == catfeeder::State::Running) {
            access.reported = catfeeder::OperationStatus::Complete;
        }
        if (controller().dispenseState() == catfeeder::State::Running) {
            dispenser.reported = catfeeder::OperationStatus::Complete;
        }
        break;
    case 'h':
        printHelp();
        break;
    case 's':
        break;
#ifndef ARDUINO
    case 'w':
        device::printWifiDetails();
        break;
#endif
    case '\r':
    case '\n':
        return;
    default:
        printLine("Unknown command. Send h for help.");
        return;
    }
    controller().update(now, readMockInputs(now));
    printStatus();
}
void initializeDemo() {
    controller(); // Initialize without commanding movement.
    printHelp();
    printStatus();
}

void updateDemo() {
    const std::uint32_t now = nowMs();
    controller().update(now, readMockInputs(now));
#ifndef ARDUINO
    if (controller().accessState() != catfeeder::State::Running &&
        controller().dispenseState() != catfeeder::State::Running) {
        catfeeder::Settings next;
        if (device::takePendingSettings(next) && controller().reconfigure(settingsConfig(next))) {
            activeSettings = next;
            printLine("Saved web settings applied. Feeding history and faults retained.");
        }
    }
#endif
    const int command = readCommand();
    if (command >= 0) {
        handleCommand(static_cast<char>(command), now);
    }
#ifndef ARDUINO
    device::publishStatus(controller().accessState(), controller().dispenseState(),
                           recognizedCat(), now);
#endif
}
} // namespace

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    initializeDemo();
}

void loop() {
    updateDemo();
}
#else
extern "C" void app_main() {
    uart_config_t uartConfig = {};
    uartConfig.baud_rate = 115200;
    uartConfig.data_bits = UART_DATA_8_BITS;
    uartConfig.parity = UART_PARITY_DISABLE;
    uartConfig.stop_bits = UART_STOP_BITS_1;
    uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartConfig.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uartConfig));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 1024, 0, nullptr, 0));
    // Keep the board's default UART console pin routing to the CH343 bridge.
    const auto servicesResult = device::startServices();
    servicesHealthy = servicesResult == ESP_OK;
    if (!servicesHealthy) {
        std::printf("Device services failed: %s. Mock movement is inhibited.\n", esp_err_to_name(servicesResult));
    }
    activeSettings = device::currentSettings();
    initializeDemo();
    while (true) {
        updateDemo();
        // Yield one scheduler tick; operation timing remains in the controller.
        vTaskDelay(1);
    }
}
#endif
