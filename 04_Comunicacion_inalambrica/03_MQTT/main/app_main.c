/*
 * PRÁCTICA DE INSTRUMENTACIÓN: COMUNICACIÓN INDUSTRIAL
 * ESP32-C6 + MQTT + LED RGB WS2812 + Potenciómetro
 * Framework: ESP-IDF 5.x
 *
 * Funcionamiento:
 * - Publica el valor del ADC (potenciómetro, canal 4 / GPIO4) cada 1 s.
 * - La página web controla el color del LED RGB WS2812 (GPIO8).
 *   Formato del payload: "R,G,B"  →  ejemplo: "255,128,0"
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_adc/adc_oneshot.h"
#include "led_strip.h"

// ------------------------------------------------------
// CREDENCIALES WIFI
// ------------------------------------------------------
#define WIFI_SSID       "DESIGNIO"  // ¡CAMBIAR POR LAS CREDENCIALES REALES!
#define WIFI_PASSWORD   "1053806111"  // ¡CAMBIAR POR LAS CREDENCIALES REALES!

#define WIFI_MAX_RETRY  5 // Número máximo de reintentos de conexión WiFi antes de dar error definitivo

// ------------------------------------------------------
// CONFIGURACIÓN MQTT
// ------------------------------------------------------
#define MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"

// ------------------------------------------------------
// TÓPICOS MQTT — deben coincidir con index.html
// ------------------------------------------------------
#define TOPIC_POT  "instrumentacion/grupo1/potenciometro"
#define TOPIC_LED  "instrumentacion/grupo1/led"

// ------------------------------------------------------
// PINES (ESP32-C6 DevKit-C1)
// ------------------------------------------------------
#define ADC_CH_POT   ADC_CHANNEL_4   // GPIO4
#define PIN_RGB_LED  GPIO_NUM_8      // WS2812 integrado

// ------------------------------------------------------
// ADC
// ------------------------------------------------------
#define ADC_MAX_VAL     4095

// ------------------------------------------------------
// INTERVALO DE PUBLICACIÓN
// ------------------------------------------------------
#define INTERVALO_MQTT_MS  1000

// ------------------------------------------------------
// WIFI — bits del event group
// ------------------------------------------------------
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static const char *TAG = "mqtt_practica";

// ------------------------------------------------------
// ESTADO GLOBAL
// ------------------------------------------------------
static esp_mqtt_client_handle_t  mqtt_client = NULL;
static EventGroupHandle_t        wifi_events;
static adc_oneshot_unit_handle_t adc_handle;
static led_strip_handle_t        led_strip;

static int pot_valor       = 0;
static int wifi_reintentos = 0;

// ------------------------------------------------------
// RGB LED — helpers
// ------------------------------------------------------
static void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (r == 0 && g == 0 && b == 0) {
        led_strip_clear(led_strip);
    } else {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

// ------------------------------------------------------
// MQTT — publicar potenciómetro
// ------------------------------------------------------
static void mqtt_publicar_pot(void)
{
    if (!mqtt_client) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pot_valor);
    esp_mqtt_client_publish(mqtt_client, TOPIC_POT, buf, 0, 0, 0);
}

// ------------------------------------------------------
// WIFI — manejador de eventos
// ------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_reintentos < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            wifi_reintentos++;
            ESP_LOGW(TAG, "WiFi desconectado — reintento %d/%d",
                     wifi_reintentos, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "No se pudo conectar al WiFi.");
        }

    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado — IP: " IPSTR, IP2STR(&e->ip_info.ip));
        wifi_reintentos = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,  &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_events,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, portMAX_DELAY);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Fallo definitivo de conexión WiFi.");
    }
}

// ------------------------------------------------------
// MQTT — manejador de eventos
// ------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t  event  = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado al broker.");
        esp_mqtt_client_subscribe(client, TOPIC_LED, 0);
        mqtt_publicar_pot();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado — reconexión automática en curso.");
        break;

    case MQTT_EVENT_DATA: {
        char topic[128] = {0};
        char data[64]   = {0};

        int tlen = event->topic_len < (int)sizeof(topic) - 1
                   ? event->topic_len : (int)sizeof(topic) - 1;
        int dlen = event->data_len  < (int)sizeof(data)  - 1
                   ? event->data_len  : (int)sizeof(data)  - 1;
        memcpy(topic, event->topic, tlen);
        memcpy(data,  event->data,  dlen);

        ESP_LOGI(TAG, "Recibido [%s] → %s", topic, data);

        if (strcmp(topic, TOPIC_LED) == 0) {
            int r = 0, g = 0, b = 0;
            if (sscanf(data, "%d,%d,%d", &r, &g, &b) == 3) {
                r = r < 0 ? 0 : (r > 255 ? 255 : r);
                g = g < 0 ? 0 : (g > 255 ? 255 : g);
                b = b < 0 ? 0 : (b > 255 ? 255 : b);
                rgb_set((uint8_t)r, (uint8_t)g, (uint8_t)b);
                ESP_LOGI(TAG, "LED RGB → (%d, %d, %d)", r, g, b);
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error en la conexión.");
        break;

    default:
        break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ------------------------------------------------------
// LED RGB — inicialización WS2812 (RMT)
// ------------------------------------------------------
static void rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num        = PIN_RGB_LED,
        .max_leds              = 1,
        .led_model             = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   // 10 MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip));
    led_strip_clear(led_strip);
    ESP_LOGI(TAG, "LED RGB WS2812 en GPIO%d inicializado.", PIN_RGB_LED);
}

// ------------------------------------------------------
// ADC — inicialización oneshot
// ------------------------------------------------------
static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,   // rango 0 – 3.3 V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CH_POT, &chan_cfg));

    ESP_LOGI(TAG, "ADC1_CH4 (GPIO4) inicializado.");
}

// ------------------------------------------------------
// TAREA — lectura ADC + publicación MQTT periódica
// ------------------------------------------------------
static void task_mqtt_publish(void *pvParameters)
{
    while (1) {
        adc_oneshot_read(adc_handle, ADC_CH_POT, &pot_valor);
        mqtt_publicar_pot();

        ESP_LOGI(TAG, "Pot ADC: %d  (%.3f V)",
                 pot_valor, (pot_valor / (float)ADC_MAX_VAL) * 3.3f);

        vTaskDelay(pdMS_TO_TICKS(INTERVALO_MQTT_MS));
    }
}

// ------------------------------------------------------
// MAIN
// ------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "=== SISTEMA MQTT (ESP-IDF %s) ===", esp_get_idf_version());
    ESP_LOGI(TAG, "Heap libre: %" PRIu32 " bytes", esp_get_free_heap_size());

    ESP_ERROR_CHECK(nvs_flash_init());

    adc_init();
    rgb_led_init();
    wifi_init();
    mqtt_init();

    xTaskCreate(task_mqtt_publish, "mqtt_publish", 4096, NULL, 4, NULL);
}
