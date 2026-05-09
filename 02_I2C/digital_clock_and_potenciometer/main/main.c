// ============================================================================
//  Practica 3.2: Reloj Digital con OLED SSD1306 y Potenciómetro - ESP32
// ============================================================================
//
//  DESCRIPCIÓN:
//  Este programa implementa un reloj digital en un ESP32 utilizando:
//
//   - Pantalla OLED SSD1306 por comunicación I2C
//   - Lectura analógica de un potenciómetro mediante ADC
//   - Comunicación UART para mostrar datos en el monitor serial
//
//  FUNCIONAMIENTO:
//
//   1. El ESP32 inicializa la comunicación I2C y el ADC.
//   2. Se muestra un reloj digital en la pantalla OLED.
//   3. Cada segundo se actualiza:
//        - Hora
//        - Minutos
//        - Segundos
//   4. También se lee continuamente el valor del potenciómetro.
//   5. La hora y el valor del potenciómetro se muestran:
//        - En la pantalla OLED
//        - En el monitor serial
//
//  HARDWARE UTILIZADO:
//
//   - ESP32
//   - Pantalla OLED SSD1306 I2C (128x64)
//   - Potenciómetro
//
//  CONEXIONES:
//
//   OLED SSD1306:
//      ESP32:    SDA -> GPIO21, SCL -> GPIO22
//      ESP32-C6: SDA -> GPIO5,  SCL -> GPIO6
//
//   Potenciómetro:
//      Señal -> GPIO34
//
// ============================================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ssd1306.h"
#include "font8x8_basic.h"

// ============================================================================
// CONFIGURACIÓN OLED
// ============================================================================

#define I2C_MASTER_SCL_IO           CONFIG_SCL_GPIO
#define I2C_MASTER_SDA_IO           CONFIG_SDA_GPIO
#define OLED_WIDTH                  128
#define OLED_HEIGHT                 64

// ============================================================================
// CONFIGURACIÓN ADC
// ============================================================================

#if CONFIG_IDF_TARGET_ESP32C6
#define POT_CHANNEL                 ADC_CHANNEL_0    // GPIO0
#define POT_GPIO                    0
#else
#define POT_CHANNEL                 ADC_CHANNEL_6    // GPIO34
#define POT_GPIO                    34
#endif

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

int hora = 3;
int minuto = 26;
int segundo = 0;

unsigned long previo = 0;
unsigned long intervalo = 1000;

static adc_oneshot_unit_handle_t adc1_handle;

// ============================================================================
// FUNCIÓN: Inicializar I2C
// ============================================================================

// ============================================================================
// FUNCIÓN: Inicializar ADC
// ============================================================================

void adc_init()
{
    adc_oneshot_unit_init_cfg_t init_config =
    {
        .unit_id = ADC_UNIT_1,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t channel_config =
    {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc1_handle,
        POT_CHANNEL,
        &channel_config
    ));
}

// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

void app_main(void)
{
    // ------------------------------------------------------------------------
    // Inicialización de periféricos
    // ------------------------------------------------------------------------

    adc_init();

    // ------------------------------------------------------------------------
    // Inicialización de pantalla OLED
    // ------------------------------------------------------------------------

    SSD1306_t dev;

    i2c_master_init(
        &dev,
        I2C_MASTER_SDA_IO,
        I2C_MASTER_SCL_IO,
        -1
    );

    ssd1306_init(
        &dev,
        OLED_WIDTH,
        OLED_HEIGHT
    );

    ssd1306_clear_screen(&dev, false);

    printf("Iniciando reloj digital con lectura de potenciómetro...\n");

    // ------------------------------------------------------------------------
    // Bucle principal
    // ------------------------------------------------------------------------

    while (1)
    {
        // --------------------------------------------------------------------
        // Lectura del potenciómetro
        // --------------------------------------------------------------------

        int valor = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, POT_CHANNEL, &valor));

        // --------------------------------------------------------------------
        // Tiempo actual del sistema
        // --------------------------------------------------------------------

        unsigned long actual = esp_log_timestamp();

        // --------------------------------------------------------------------
        // Actualización del reloj cada 1 segundo
        // --------------------------------------------------------------------

        if (actual - previo >= intervalo)
        {
            previo = actual;

            segundo++;

            // ---------------------------------------------------------------
            // Control de segundos
            // ---------------------------------------------------------------

            if (segundo >= 60)
            {
                segundo = 0;
                minuto++;

                // -----------------------------------------------------------
                // Control de minutos
                // -----------------------------------------------------------

                if (minuto >= 60)
                {
                    minuto = 0;
                    hora++;

                    // -------------------------------------------------------
                    // Control de horas
                    // -------------------------------------------------------

                    if (hora >= 24)
                    {
                        hora = 0;
                    }
                }
            }

            // ---------------------------------------------------------------
            // Mostrar información por UART
            // ---------------------------------------------------------------

            printf(
                "Hora actual: %02d:%02d:%02d | Potenciometro: %d\n",
                hora,
                minuto,
                segundo,
                valor
            );
        }

        // --------------------------------------------------------------------
        // Variables para mostrar texto en OLED
        // --------------------------------------------------------------------

        char reloj[20];
        char pot[20];

        sprintf(
            reloj,
            "%02d:%02d:%02d",
            hora,
            minuto,
            segundo
        );

        sprintf(
            pot,
            "POT: %d",
            valor
        );

        // --------------------------------------------------------------------
        // Limpiar pantalla OLED
        // --------------------------------------------------------------------

        ssd1306_clear_screen(&dev, false);

        // --------------------------------------------------------------------
        // Mostrar reloj en OLED
        // --------------------------------------------------------------------

        ssd1306_display_text(
            &dev,
            1,
            reloj,
            strlen(reloj),
            false
        );

        // --------------------------------------------------------------------
        // Mostrar valor del potenciómetro
        // --------------------------------------------------------------------

        ssd1306_display_text(
            &dev,
            5,
            pot,
            strlen(pot),
            false
        );

        // --------------------------------------------------------------------
        // Pequeño retardo
        // --------------------------------------------------------------------

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
