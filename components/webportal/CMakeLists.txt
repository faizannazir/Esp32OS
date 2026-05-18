idf_component_register(SRCS "src/webportal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server os_logging os_networking os_kernel os_shell os_ota)
