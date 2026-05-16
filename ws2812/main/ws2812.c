#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

// 핀 번호 설정 (ESP32-S3 Zero 내장 LED는 보통 21번)
#define BLINK_GPIO 21       
#define MAX_LEDS 1          // 제어할 LED 1개
#define BLINK_PERIOD 1000   // 1초 대기

static const char *TAG = "LED_RGB_CYCLE";
static led_strip_handle_t led_strip;

void configure_led(void)
{
    ESP_LOGI(TAG, "WS2812 RGB LED 설정을 시작합니다.");
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = MAX_LEDS,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void app_main(void)
{
    configure_led();
    int state = 0; // 0~5까지 순환하는 상태 변수

    while (1) {
        // 먼저 이전 상태의 색상을 지웁니다.
        led_strip_clear(led_strip); 

        switch (state) {
            case 0: // 빨강 켜기
                led_strip_set_pixel(led_strip, 0, 100, 0, 0); 
                ESP_LOGI(TAG, "LED ON (RED)");
                break;
            case 1: // 꺼짐
                ESP_LOGI(TAG, "LED OFF");
                break;
            case 2: // 초록 켜기
                led_strip_set_pixel(led_strip, 0, 0, 100, 0); 
                ESP_LOGI(TAG, "LED ON (GREEN)");
                break;
            case 3: // 꺼짐
                ESP_LOGI(TAG, "LED OFF");
                break;
            case 4: // 파랑 켜기
                led_strip_set_pixel(led_strip, 0, 0, 0, 100); 
                ESP_LOGI(TAG, "LED ON (BLUE)");
                break;
            case 5: // 꺼짐
                ESP_LOGI(TAG, "LED OFF");
                break;
            case 6: 
                led_strip_set_pixel(led_strip, 0, 100, 100, 100); 
                ESP_LOGI(TAG, "LED ON (WHITE)");
                break;
            case 7: // 꺼짐
                ESP_LOGI(TAG, "LED OFF");
                break;
        }
        
        // 설정한 색상(또는 꺼짐 상태)을 LED로 전송
        led_strip_refresh(led_strip);

        // 상태를 1 증가시키고, 7을 넘어가면 다시 0(빨강)으로 초기화
        state++;
        if (state > 7) {
            state = 0;
        }

        // 1초(1000ms) 대기
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD));
    }
}