idf_component_register(
    SRCS
        "src/os_mqtt.c"
        "test/os_mqtt_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging os_networking mqtt freertos esp_wifi
)
