/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/* ─── Includes ─────────────────────────────────────────────────────────────── */
#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "led.h"
#include "esp_adc/adc_oneshot.h"

/* ─── Declaraciones de funciones de librería ────────────────────────────────── */
void ble_store_config_init(void);

/* ─── Declaraciones de funciones privadas ───────────────────────────────────── */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void pot_task(void *param);

/* ─── Funciones privadas ────────────────────────────────────────────────────── */

/*
 *  Callbacks de eventos del stack BLE:
 *      - on_stack_reset: se llama cuando el host reinicia el stack por errores
 *      - on_stack_sync:  se llama cuando el host se sincroniza con el controlador
 */
static void on_stack_reset(int reason) {
    ESP_LOGI(TAG, "stack nimble reiniciado, motivo: %d", reason);
}

static void on_stack_sync(void) {
    adv_init();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb          = on_stack_reset;
    ble_hs_cfg.sync_cb           = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "tarea nimble host iniciada!");
    nimble_port_run();
    vTaskDelete(NULL);
}

/*
 *  Lee el potenciómetro cada 100 ms y actualiza el brillo del LED.
 *  Conexión física: POT_ADC_CHANNEL = ADC1_CH4 = GPIO4
 *      - Pin central del pot → GPIO4
 *      - Extremos del pot    → 3.3 V y GND
 */
static void pot_task(void *param) {
    /* Inicialización del ADC en modo oneshot */
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   /* rango 0–3.1 V */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, POT_ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "tarea potenciómetro iniciada — GPIO%d", POT_ADC_GPIO);

    int     raw            = 0;
    uint8_t brightness     = 0;
    uint8_t brightness_pct = 0;
    uint8_t last_pct       = 0xFF;   /* valor anterior para filtrar mensajes repetidos */

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, POT_ADC_CHANNEL, &raw));

        /* Mapear ADC (0–4095) → brillo WS2812 (0–255) */
        brightness = (uint8_t)(raw * 255 / 4095);
        led_set_brightness(brightness);

        /* Enviar brillo por NUS solo cuando cambia ≥ 2% */
        brightness_pct = (uint8_t)(raw * 100 / 4095);
        uint8_t diff   = brightness_pct > last_pct
                         ? brightness_pct - last_pct
                         : last_pct - brightness_pct;
        if (diff >= 2) {
            char msg[20];
            snprintf(msg, sizeof(msg), "Brillo: %u%%\n", brightness_pct);
            nus_send_string(msg);
            last_pct = brightness_pct;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ─── Punto de entrada principal ────────────────────────────────────────────── */
void app_main(void) {
    int       rc  = 0;
    esp_err_t ret;

    /* Inicialización del LED */
    led_init();

    /*
     * Inicialización de NVS flash
     * Dependencia del stack BLE para almacenar configuraciones
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "error al inicializar NVS flash, código: %d", ret);
        return;
    }

    /* Inicialización del stack NimBLE */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "error al inicializar stack nimble, código: %d", ret);
        return;
    }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* Inicialización del servicio GAP */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "error al inicializar servicio GAP, código: %d", rc);
        return;
    }
#endif

    /* Inicialización del servidor GATT */
    rc = gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "error al inicializar servidor GATT, código: %d", rc);
        return;
    }

    /* Inicialización de configuración del host NimBLE */
    nimble_host_config_init();

    /* Crear tareas FreeRTOS y retornar */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
    xTaskCreate(pot_task,         "Pot ADC",     4 * 1024, NULL, 4, NULL);
    return;
}
