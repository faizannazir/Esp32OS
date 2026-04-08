idf_component_register(
    SRCS "src/os_networking.c"
    INCLUDE_DIRS "include"
    REQUIRES
        os_logging
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        nvs_flash
        lwip
        freertos
)
