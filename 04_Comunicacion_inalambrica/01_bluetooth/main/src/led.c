/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "led.h"
#include "common.h"

/* Private variables */
static uint8_t led_state;

#ifdef CONFIG_BLINK_LED_STRIP
static led_strip_handle_t led_strip;
static uint8_t            led_brightness = 32;    /* 0-255, por defecto tenue */
static uint8_t            led_color_r    = 255;   /* color base a pleno brillo */
static uint8_t            led_color_g    = 255;
static uint8_t            led_color_b    = 255;
#endif

/* Public functions */
uint8_t get_led_state(void) { return led_state; }

#ifdef CONFIG_BLINK_LED_STRIP

/* Aplica el color actual escalado por el brillo actual */
static void led_apply(void) {
    uint8_t r = (uint8_t)((uint16_t)led_color_r * led_brightness / 255);
    uint8_t g = (uint8_t)((uint16_t)led_color_g * led_brightness / 255);
    uint8_t b = (uint8_t)((uint16_t)led_color_b * led_brightness / 255);
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

void led_on(void) {
    led_apply();
    led_state = true;
}

void led_off(void) {
    led_strip_clear(led_strip);
    led_state = false;
}

void led_set_brightness(uint8_t brightness) {
    led_brightness = brightness;
    if (led_state) {
        led_apply();
    }
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    led_color_r = r;
    led_color_g = g;
    led_color_b = b;
    led_state   = true;
    led_apply();
}

void led_init(void) {
    ESP_LOGI(TAG, "example configured to blink addressable led!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(
        led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "unsupported LED strip backend"
#endif
    /* Set all LED off to clear all pixels */
    led_off();
}

#elif CONFIG_BLINK_LED_GPIO

void led_on(void) { gpio_set_level(CONFIG_BLINK_GPIO, true); led_state = true; }

void led_off(void) { gpio_set_level(CONFIG_BLINK_GPIO, false); led_state = false; }

void led_set_brightness(uint8_t brightness) {
    (void)brightness;  /* GPIO no soporta PWM */
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    (void)r; (void)g; (void)b;  /* GPIO no soporta color */
    led_on();
}

void led_init(void) {
    ESP_LOGI(TAG, "example configured to blink gpio led!");
    gpio_reset_pin(CONFIG_BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(CONFIG_BLINK_GPIO, GPIO_MODE_OUTPUT);
}

#else
#error "unsupported LED type"
#endif
