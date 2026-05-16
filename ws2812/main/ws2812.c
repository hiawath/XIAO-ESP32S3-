#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

// 핀 설정: ESP32-S3 Zero는 21번, 일반 S3 DevKitC는 48번 사용
#define BLINK_GPIO 21
#define BLINK_PERIOD 1000

static const char *TAG = "LED_TEST";
static led_strip_handle_t led_strip;

void configure_led(void)
{
    ESP_LOGI(TAG, "WS2812 RGB LED 설정을 시작합니다.");

    // LED 스트립 하드웨어 설정
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // 보드에 내장된 LED 1개
    };

    // RMT 백엔드 설정 (WS2812 통신용)
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz 해상도
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    // 초기 상태: LED 끄기
    led_strip_clear(led_strip);
}

void app_main(void)
{
    configure_led();
    bool led_state = false;

    while (1) {
        if (led_state) {
            // LED 켜기: R, G, B 값 설정 (0~255)
            // 주의: 너무 높은 값을 넣으면 눈이 부시므로 10~20 수준으로 테스트 권장
            led_strip_set_pixel(led_strip, 0, 0, 20, 0); // 초록색(G) 켜기
            led_strip_refresh(led_strip);
            ESP_LOGI(TAG, "LED ON (Green)");
        } else {
            // LED 끄기
            led_strip_clear(led_strip);
            ESP_LOGI(TAG, "LED OFF");
        }

        led_state = !led_state;
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD));
    }
}