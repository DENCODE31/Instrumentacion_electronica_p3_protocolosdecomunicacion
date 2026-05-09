idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif esp_wifi
                                  esp_adc espressif__led_strip
                    INCLUDE_DIRS ".")
