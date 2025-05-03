#include <stdio.h>
#include "sdkconfig.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "esp_tls.h"
#include <fcntl.h>

static const char *TAG = "MOISTURE";

// Pin and channel definitions
#define MOISTURE_ADC_CHANNEL  ADC_CHANNEL_2   // corresponds to GPIO2
#define ADC_ATTEN             ADC_ATTEN_DB_12 // full-scale ~3.3V (adjust per your needs)
#define ADC_BITWIDTH          ADC_BITWIDTH_DEFAULT

// Relay GPIO
#define RELAY_GPIO    GPIO_NUM_5  

// Wi-Fi Configurations
// at home: TP-Link_31B2, sweets1303 
#define WIFI_SSID     "sswaroops"    // Replace with your Wi-Fi SSID
#define WIFI_PASS     "astu123$" // Replace with your Wi-Fi password
#define WIFI_MAX_RETRY  5

// Server configuration
#define SERVER_IP "10.0.0.235"  // Replace with the server's IP address
#define SERVER_PORT 12345          // Replace with the server's port

// Wi-Fi event handler
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

extern const uint8_t ca_cert_pem_start[]   asm("_binary_ca_cert_pem_start");
extern const uint8_t ca_cert_pem_end[]     asm("_binary_ca_cert_pem_end");
extern const uint8_t client_cert_pem_start[] asm("_binary_client_cert_pem_start");
extern const uint8_t client_cert_pem_end[]   asm("_binary_client_cert_pem_end");
extern const uint8_t client_key_pem_start[]  asm("_binary_client_key_pem_start");
extern const uint8_t client_key_pem_end[]    asm("_binary_client_key_pem_end");

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying to connect to Wi-Fi...");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Wi-Fi initialization function
void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to Wi-Fi");
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }
}

// TCP Socket function to send data to the server
void send_to_server(int moisture_value, esp_tls_t *tls) {
    //struct sockaddr_in dest_addr;
    //dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    //dest_addr.sin_family = AF_INET;
    //dest_addr.sin_port = htons(SERVER_PORT);
    //int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    //if (sock < 0) {
    //    ESP_LOGE(TAG, "Socket creation failed!");
    //    return;
    //}

    //int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    //if (err < 0) {
    //    ESP_LOGE(TAG, "Unable to connect to server: errno %d", errno);
    //    return;
    //}

    char payload[64];
    int len = snprintf(payload, sizeof(payload), "Moisture Value: %d", moisture_value);
    
    //int err = send(sock, payload, strlen(payload), 0);
    int err = esp_tls_conn_write(tls, (const unsigned char *)payload, len);
    if (err < 0) {
        ESP_LOGE(TAG, "Error sending data: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Data sent to server: %s", payload);
    }

}

void app_main(void)
{
    // --- 1) Initialize NVS ---
    ESP_ERROR_CHECK(nvs_flash_init());

    // --- 2) Initialize Wi-Fi ---
    wifi_init_sta();

    // --- 3) Install ADC one-shot driver ---
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,    // not using ultra-low-power mode
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));  

    // --- 4) Configure the channel ---
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(adc_handle, MOISTURE_ADC_CHANNEL, &chan_cfg)
    );

    // --- 5) (Optional) Set up calibration ---
    adc_cali_handle_t cal_handle = NULL;
    bool do_calibration = false;
    {
        adc_cali_curve_fitting_config_t curve_cfg = {
            .unit_id = ADC_UNIT_1,
            .chan    = MOISTURE_ADC_CHANNEL,
            .atten   = ADC_ATTEN,
            .bitwidth= ADC_BITWIDTH,
        };
        if (adc_cali_create_scheme_curve_fitting(&curve_cfg, &cal_handle) == ESP_OK) {
            ESP_LOGI(TAG, "Curve-fitting calibration enabled");
            do_calibration = true;
        } 
    }

    // --- 6) Establish TLS connection ---
    esp_tls_cfg_t tls_cfg = {
        .cacert_pem_buf = ca_cert_pem_start,
        .cacert_pem_bytes = ca_cert_pem_end - ca_cert_pem_start,
        .clientcert_pem_buf = client_cert_pem_start,
        .clientcert_pem_bytes = client_cert_pem_end - client_cert_pem_start,
        .clientkey_pem_buf = client_key_pem_start,
        .clientkey_pem_bytes = client_key_pem_end - client_key_pem_start,
        //.non_block = true,
    };

    esp_tls_t *tls = esp_tls_init();

    int ret = esp_tls_conn_new_sync(SERVER_IP, strlen("192.168.0.101"), 12345, &tls_cfg, tls);

    if(ret != 1){
        ESP_LOGE(TAG, "TLS connection failed");
        esp_tls_conn_destroy(tls);
        return;
    }
    ESP_LOGI(TAG, "TLS connection OK");

    // -- 7) Setup relay GPIO pin
    // configure the relay pin as push-pull output, start low (relay off)
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_GPIO, 1);
    
    // Make TLS reads non-blocking
   // int fd = tls->sockfd;
   // int flags = fcntl(fd, F_GETFL, 0);
   // fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // --- 7) Read loop ---
    while (1) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, MOISTURE_ADC_CHANNEL, &raw));
        ESP_LOGI(TAG, "Raw ADC[%d]: %d", MOISTURE_ADC_CHANNEL, raw);

        if (do_calibration) {
            int voltage_mv;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cal_handle, raw, &voltage_mv));
            ESP_LOGI(TAG, "Calibrated: %d mV", voltage_mv);
            send_to_server(voltage_mv, tls);  // Send the moisture data to the server
        }
        // — 2) non-blocking read for any command —
        char cmd_buf[16];
        int r = esp_tls_conn_read(tls,
                                  (unsigned char*)cmd_buf,
                                  sizeof(cmd_buf)-1);
        if (r > 0) {
            cmd_buf[r] = '\0';
            ESP_LOGI(TAG, "Command from server: %s", cmd_buf);
            if (strcmp(cmd_buf, "WATER_ON") == 0) {
                // turn relay on for 2 seconds
                gpio_set_level(RELAY_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(2000));
                gpio_set_level(RELAY_GPIO, 1);
            } else if (strcmp(cmd_buf, "WATER_OFF") == 0){
                gpio_set_level(RELAY_GPIO, 1);
            }
        } else {
            gpio_set_level(RELAY_GPIO, 1);
        }


        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    esp_tls_conn_destroy(tls);
    ESP_LOGI(TAG, "TLS connection closed");
    //shutdown(sock, 0);
    //close(sock);
}
