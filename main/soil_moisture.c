#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_tls.h"

static const char *TAG = "MOISTURE_NET";

/* --- ADC setup from earlier example --- */
#define MOISTURE_ADC_CHANNEL  ADC_CHANNEL_2  // GPIO2
#define ADC_ATTEN             ADC_ATTEN_DB_12
#define ADC_BITWIDTH          ADC_BITWIDTH_DEFAULT

static adc_oneshot_unit_handle_t adc_handle;

/* --- Embedded CA cert --- */
extern const unsigned char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const unsigned char ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");

/* --- 1) Initialize Wi‑Fi in Station mode --- */
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    (void) sta;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = "TP-Link_31B2",
            .password = "sweets1303",
            /* Optional: .scan_method, .threshold settings */
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Waiting for IP address...");
    /* Simple block until got IP—production code should use event handlers */
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Wi-Fi should be connected now");
}
 
/* --- 2) Task to read ADC and send over TLS every 10s --- */
static void transmit_task(void *arg)
{
    /* Configure ADC channel once */
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle,
                                               MOISTURE_ADC_CHANNEL,
                                               &ch_cfg));

    /* Set up TLS config */
    esp_tls_cfg_t tls_cfg = {
        .cacert_pem_buf  = ca_cert_pem_start,
        .cacert_pem_bytes= ca_cert_pem_end - ca_cert_pem_start,
        .timeout_ms      = 5000,
    };

    while (true) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle,
                                         MOISTURE_ADC_CHANNEL,
                                         &raw));
        ESP_LOGI(TAG, "Moisture(raw) = %d", raw);

        /* 2.1) establish a TLS connection */
        esp_tls_t *tls = esp_tls_init();
        //struct esp_tls *tls = esp_tls_conn_new(
        //    "my.server.com", strlen("my.server.com"),
        //    443, &tls_cfg
        //);
        // :contentReference[oaicite:0]{index=0}
        if (!tls) {
            ESP_LOGE(TAG, "TLS init failed");
            return;
        }

        int ret = esp_tls_conn_new_sync(
            "192.168.0.101", strlen("192.168.0.101"),
            8443, &tls_cfg, tls
        );
        if (ret != 1) {
            ESP_LOGE(TAG, "TLS sync connect failed: %d", ret);
            esp_tls_conn_destroy(tls);
            return;
        }
        /* 2.2) format a simple JSON payload */
        char payload[64];
        int len = snprintf(payload, sizeof(payload),
                           "{\"moisture\":%d}\r\n", raw);

        /* 2.3) send it over the socket */
        ssize_t written = esp_tls_conn_write(
            tls, (const unsigned char*)payload, len
        );
        if (written < 0) {
            ESP_LOGE(TAG, "esp_tls_conn_write - %d", (int)written);
        } else {
            ESP_LOGI(TAG, "Sent %d bytes", (int)written);
        }

        /* 2.4) tear down TLS session */
        esp_tls_conn_destroy(tls);

    //wait:
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* --- 3) app_main sets up ADC, Wi‑Fi, then starts the task --- */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting up…");

    /* ADC one‑shot init */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    /* Wi‑Fi init */
    wifi_init_sta();  // :contentReference[oaicite:1]{index=1}

    /* Launch Tx task */
    xTaskCreate(transmit_task, "tx_task",
                6*1024, NULL, tskIDLE_PRIORITY+1, NULL);
}
