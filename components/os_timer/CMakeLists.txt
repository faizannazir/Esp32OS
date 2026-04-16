idf_component_register(
    SRCS
        "src/os_timer.c"
        "test/os_timer_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging freertos esp_driver_uart lwip esp_timer
)