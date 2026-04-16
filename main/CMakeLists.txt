idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        os_logging
        os_kernel
        os_fs
        os_hal
        os_networking
        os_pwm
        os_timer
        os_ipc
        os_mqtt
        os_ota
        os_shell
        os_drivers
        nvs_flash
        esp_system
        freertos
)
