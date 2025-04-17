#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

static const char *TAG = "MOISTURE";

// Pin and channel definitions
#define MOISTURE_ADC_CHANNEL  ADC_CHANNEL_2   // corresponds to GPIO2
#define ADC_ATTEN             ADC_ATTEN_DB_12 // full-scale ~3.3V (adjust per your needs)
#define ADC_BITWIDTH          ADC_BITWIDTH_DEFAULT

void app_main(void)
{
    // --- 1) Install ADC one-shot driver ---
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,    // not using ultra-low-power mode
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));  // :contentReference[oaicite:1]{index=1}

    // --- 2) Configure the channel ---
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(adc_handle, MOISTURE_ADC_CHANNEL, &chan_cfg)
    );  // :contentReference[oaicite:2]{index=2}

    // --- 3) (Optional) Set up calibration to convert raw->mV ---
    adc_cali_handle_t cal_handle = NULL;
    bool do_calibration = false;
    // Try curve-fitting first, fall back to line-fitting if available
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

    // --- 4) Read loop ---
    while (1) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, MOISTURE_ADC_CHANNEL, &raw));
        ESP_LOGI(TAG, "Raw ADC[%d]: %d", MOISTURE_ADC_CHANNEL, raw);

        if (do_calibration) {
            int voltage_mv;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cal_handle, raw, &voltage_mv));
            ESP_LOGI(TAG, "Calibrated: %d mV", voltage_mv);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // (never reached in this example) cleanup:
    // adc_cali_delete_scheme_curve_fitting(cal_handle)  etc.
    // adc_oneshot_del_unit(adc_handle);
}
