/**
 * ESP32-S3 + OV2640 카메라 예제
 *
 * 보드  : Seeed XIAO ESP32S3 Sense
 * 기능  : MJPEG HTTP 스트리밍 서버 (포트 80)
 *         GET /         → 웹 뷰어
 *         GET /stream   → MJPEG 스트림
 *         GET /capture  → 단일 JPEG
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "driver/gpio.h"

// ─── Wi-Fi 설정 ───────────────────────────────────────────────────────────────
#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WIFI_PASSWORD
#define MAX_RETRY      5

static const char *TAG = "CAM_MAIN";

// ─── 카메라 핀 정의 (Seeed XIAO ESP32S3 Sense) ───────────────────────────────
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    10
#define CAM_PIN_SIOD    40   // SDA
#define CAM_PIN_SIOC    39   // SCL
#define CAM_PIN_D7      48
#define CAM_PIN_D6      11
#define CAM_PIN_D5      12
#define CAM_PIN_D4      14
#define CAM_PIN_D3      16
#define CAM_PIN_D2      18
#define CAM_PIN_D1      17
#define CAM_PIN_D0      15
#define CAM_PIN_VSYNC   38
#define CAM_PIN_HREF    47
#define CAM_PIN_PCLK    13

// ─── Wi-Fi 이벤트 그룹 ────────────────────────────────────────────────────────
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// ─── 카메라 초기화 ────────────────────────────────────────────────────────────
static esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn       = CAM_PIN_PWDN,
        .pin_reset      = CAM_PIN_RESET,
        .pin_xclk       = CAM_PIN_XCLK,
        .pin_sccb_sda   = CAM_PIN_SIOD,
        .pin_sccb_scl   = CAM_PIN_SIOC,
        .pin_d7         = CAM_PIN_D7,
        .pin_d6         = CAM_PIN_D6,
        .pin_d5         = CAM_PIN_D5,
        .pin_d4         = CAM_PIN_D4,
        .pin_d3         = CAM_PIN_D3,
        .pin_d2         = CAM_PIN_D2,
        .pin_d1         = CAM_PIN_D1,
        .pin_d0         = CAM_PIN_D0,
        .pin_vsync      = CAM_PIN_VSYNC,
        .pin_href       = CAM_PIN_HREF,
        .pin_pclk       = CAM_PIN_PCLK,

        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,

        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_VGA,   // 640x480
        .jpeg_quality   = 12,              // 0~63, 낮을수록 고품질
        .fb_count       = 2,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "카메라 초기화 실패: 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_lenc(s, 1);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
    }

    ESP_LOGI(TAG, "카메라 초기화 성공");
    return ESP_OK;
}

// ─── Wi-Fi 이벤트 핸들러 ──────────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi 재연결 시도 %d/%d", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP 주소: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ─── Wi-Fi 초기화 ─────────────────────────────────────────────────────────────
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi 연결 성공");
    } else {
        ESP_LOGE(TAG, "Wi-Fi 연결 실패");
    }
}

// ─── MJPEG 스트림 핸들러 ──────────────────────────────────────────────────────
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char part_buf[64];
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { ESP_LOGE(TAG, "프레임 버퍼 획득 실패"); break; }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
    }
    return res;
}

// ─── 단일 캡처 핸들러 ─────────────────────────────────────────────────────────
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    ESP_LOGI(TAG, "캡처: %u bytes", fb->len);
    esp_camera_fb_return(fb);
    return ESP_OK;
}

// ─── 인덱스 페이지 핸들러 ─────────────────────────────────────────────────────
static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<title>XIAO ESP32S3 카메라</title>"
    "<style>"
    "body{background:#0d0d0d;color:#e0e0e0;font-family:monospace;text-align:center;margin:0;padding:24px}"
    "h1{color:#5af;letter-spacing:2px;margin-bottom:4px}"
    "p.sub{color:#555;font-size:12px;margin-bottom:16px}"
    "img{border:2px solid #5af;border-radius:6px;max-width:100%;display:block;margin:0 auto}"
    "button{margin:8px;padding:10px 22px;background:#5af;color:#000;"
    "border:none;border-radius:4px;cursor:pointer;font-size:13px;font-weight:bold}"
    "button:hover{background:#38d}"
    "</style></head>"
    "<body>"
    "<h1>&#128247; XIAO ESP32S3 Sense</h1>"
    "<p class='sub'>OV2640 MJPEG Stream</p>"
    "<img id='s' src='/stream'/>"
    "<div style='margin-top:16px'>"
    "<button onclick=\"document.getElementById('s').src='/stream?t='+Date.now()\">&#128257; 재시작</button>"
    "<button onclick=\"window.open('/capture','_blank')\">&#128248; 캡처</button>"
    "</div>"
    "<p style='color:#444;font-size:11px;margin-top:20px'>/stream &nbsp;|&nbsp; /capture</p>"
    "</body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

// ─── 웹 서버 시작 ─────────────────────────────────────────────────────────────
static void start_webserver(void)
{
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "웹 서버 시작 실패"); return;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",        .method = HTTP_GET, .handler = index_handler   },
        { .uri = "/stream",  .method = HTTP_GET, .handler = stream_handler  },
        { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler },
    };
    for (int i = 0; i < 3; i++) httpd_register_uri_handler(server, &uris[i]);
    ESP_LOGI(TAG, "웹 서버 시작 (포트 80)");
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "XIAO ESP32S3 Sense OV2640 시작");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(camera_init());
    wifi_init_sta();
    start_webserver();

    ESP_LOGI(TAG, "준비 완료! 브라우저에서 http://<위 IP주소> 접속");

    while (1) {
        ESP_LOGI(TAG, "여유 힙: %lu KB (최소: %lu KB)",
                 esp_get_free_heap_size() / 1024,
                 esp_get_minimum_free_heap_size() / 1024);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}