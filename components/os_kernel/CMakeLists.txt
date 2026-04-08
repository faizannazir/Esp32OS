idf_component_register(
    SRCS "src/os_kernel.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging driver esp_driver_uart esp_timer freertos esp_system lwip
)
