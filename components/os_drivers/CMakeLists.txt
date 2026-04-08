idf_component_register(
    SRCS "src/os_drivers.c"
    INCLUDE_DIRS "include"
    REQUIRES
        os_logging
    driver
    esp_driver_gpio
    esp_driver_spi
    esp_driver_uart
    esp_adc
        freertos
        lwip
)
