idf_component_register(SRCS "environment.c" "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES BSP clothes_control freertos log esp_timer console)
