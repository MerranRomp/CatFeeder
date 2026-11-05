idf_component_register(
    SRCS "Settings.cpp" "Controller.cpp" "esp32_main.cpp" "DeviceServices.cpp"
    INCLUDE_DIRS "../include" "../simulation"
    REQUIRES driver esp_timer freertos esp_wifi esp_netif esp_event nvs_flash esp_http_server json
    EMBED_TXTFILES "../web/index.html"
)
