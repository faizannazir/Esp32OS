idf_component_register(
    SRCS
        "src/os_pwm.c"
        "test/os_pwm_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging esp_driver_ledc freertos
)
