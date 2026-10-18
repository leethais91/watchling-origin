#include "status_led.h"

#include "esp_check.h"
#include "led_indicator.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "status_led";

static led_indicator_handle_t s_led;

esp_err_t status_led_init(void)
{
    led_indicator_handle_t leds[BSP_LED_NUM];
    ESP_RETURN_ON_ERROR(bsp_led_indicator_create(leds, NULL, BSP_LED_NUM), TAG, "LED unavailable");
    s_led = leds[BSP_LED_1];
    return led_indicator_start(s_led, BSP_LED_OFF);
}

void status_led_set_attention(bool on)
{
    if (s_led == NULL) {
        return;
    }
    /* led_indicator plays the highest-priority active pattern: stop the one not wanted. */
    led_indicator_stop(s_led, on ? BSP_LED_OFF : BSP_LED_BLINK_FAST);
    led_indicator_start(s_led, on ? BSP_LED_BLINK_FAST : BSP_LED_OFF);
}
