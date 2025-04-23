idf_component_register(SRCS "soil_moisture.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp-tls lwip esp_adc esp_wifi esp_event nvs_flash
                    EMBED_TXTFILES ca.cert.pem client.cert.pem client.key.pem)


