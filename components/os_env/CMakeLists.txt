idf_component_register(
    SRCS
        "src/os_env.c"
        "test/os_env_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging freertos nvs_flash driver esp_driver_uart lwip
)
