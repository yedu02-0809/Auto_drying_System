idf_component_register(SRCS "environment.c" "main.c" "web_portal.c" "web_status.c" "display.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES BSP clothes_control freertos log esp_timer console
                       REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash)
