idf_component_register(
    SRCS
        "src/os_ota.c"
        "test/os_ota_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging app_update esp_http_client esp_https_ota freertos
)
