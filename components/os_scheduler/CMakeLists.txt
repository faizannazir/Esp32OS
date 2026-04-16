idf_component_register(
    SRCS
        "src/os_scheduler.c"
        "test/os_scheduler_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging os_shell os_kernel freertos driver esp_driver_uart lwip
)
