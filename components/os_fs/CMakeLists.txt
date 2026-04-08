idf_component_register(
    SRCS "src/os_fs.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging spiffs vfs lwip esp_driver_uart
)
