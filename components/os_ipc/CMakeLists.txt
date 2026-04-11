idf_component_register(
    SRCS
        "src/os_ipc.c"
        "test/os_ipc_test.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging freertos
)
