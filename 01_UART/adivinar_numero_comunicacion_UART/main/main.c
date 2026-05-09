// ===============================================================
//  Juego de Adivinar el Número por UART - ESP-IDF
// ===============================================================
//  Adaptado para ESP-IDF por Dennir
// ===============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_system.h"

#include "esp_random.h"
// ===============================================================
// Variables globales
// ===============================================================

int numeroSecreto;
int intento;
int intentos = 0;
int rangoMin = 1;
int rangoMax = 100;

bool juegoIniciado = false;
bool adivinado = false;

// ===============================================================
// Configuración UART
// ===============================================================

#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);
}

// ===============================================================
// Función principal
// ===============================================================

void app_main(void)
{
    uart_init();

    char data[BUF_SIZE];

    printf("=== Juego de Adivinar el Número ===\n");
    printf("Envía dos números separados por espacio para definir el rango (Ejemplo: 10 50)\n");

    while (1)
    {
        // Leer datos UART
        int len = uart_read_bytes(UART_PORT,
                                  (uint8_t *)data,
                                  BUF_SIZE - 1,
                                  pdMS_TO_TICKS(100));

        if (len > 0)
        {
            data[len] = '\0';

            // ===================================================
            // Inicio del juego
            // ===================================================

            if (!juegoIniciado)
            {
                if (sscanf(data, "%d %d", &rangoMin, &rangoMax) == 2)
                {
                    if (rangoMin < rangoMax && rangoMax > 0)
                    {
                        numeroSecreto = (esp_random() % (rangoMax - rangoMin + 1)) + rangoMin;

                        juegoIniciado = true;
                        intentos = 0;

                        printf("Juego iniciado. Adivina el número entre %d y %d\n",
                               rangoMin,
                               rangoMax);
                    }
                }
            }

            // ===================================================
            // Intentos del usuario
            // ===================================================

            else
            {
                if (sscanf(data, "%d", &intento) == 1)
                {
                    if (intento >= rangoMin && intento <= rangoMax)
                    {
                        intentos++;

                        if (intento == numeroSecreto)
                        {
                            printf("¡Correcto! Adivinaste el número en %d intentos.\n",
                                   intentos);

                            adivinado = true;
                        }
                        else if (intento < numeroSecreto)
                        {
                            printf("El número es mayor.\n");
                        }
                        else
                        {
                            printf("El número es menor.\n");
                        }
                    }
                }
            }
        }

        // =======================================================
        // Reinicio del juego
        // =======================================================

        if (adivinado)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));

            printf("Envía nuevo rango para jugar otra vez (Ejemplo: 10 100)\n");

            juegoIniciado = false;
            adivinado = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}