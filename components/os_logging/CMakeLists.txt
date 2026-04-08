idf_component_register(
    SRCS "src/os_logging.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart
    REQUIRES driver esp_timer freertos
)
