/**
 * SmartElderCare - Wi-Fi + MJPEG HTTP 服务实现
 */
#include "wifi_mjpeg.h"
#include "camera_ov2640.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

static const char *TAG = "wifi_mjpeg";

/* ==== Wi-Fi 状态 ==== */
static EventGroupHandle_t s_wifi_evt;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi 断开，5s 后重连 ...");
        xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED_BIT);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已连接 Wi-Fi, IP = " IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "在电脑浏览器打开: http://" IPSTR "/stream", IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_mjpeg_wifi_start(void)
{
    /* NVS 初始化（Wi-Fi 需要） */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    s_wifi_evt = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       &wifi_event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid,     ELDER_WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, ELDER_WIFI_PASS, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi 已启动，SSID=%s，等待连接 ...", ELDER_WIFI_SSID);
    return ESP_OK;
}

/* ==== 报警标志（电脑端识别到跌倒后 POST /alarm 置位） ==== */
static volatile bool s_fall_alarm = false;

bool wifi_mjpeg_pop_fall_alarm(void)
{
    bool v = s_fall_alarm;
    s_fall_alarm = false;
    return v;
}

/* ==== MJPEG 推流处理 ==== */
#define PART_BOUNDARY "elderframe"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART_HDR =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    char part_buf[64];
    uint32_t fail_cnt = 0;
    uint32_t ok_cnt   = 0;
    while (true) {
        camera_fb_t *fb = camera_ov2640_capture();
        if (!fb) {
            fail_cnt++;
            /* 只在第 1 次和每 20 次打印一次, 避免刷屏 */
            if (fail_cnt == 1 || fail_cnt % 20 == 0) {
                ESP_LOGW(TAG, "抓帧失败 (累计 %lu 次, 成功 %lu 帧)",
                         (unsigned long)fail_cnt, (unsigned long)ok_cnt);
            }
            /* 连续失败太多次直接断开, 让客户端重连触发重新协商 */
            if (fail_cnt > 200 && ok_cnt == 0) {
                ESP_LOGE(TAG, "连续 200 次抓不到帧, 断开客户端");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        ok_cnt++;
        fail_cnt = 0;

        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART_HDR, fb->len);

        if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK ||
            httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK ||
            httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
            camera_ov2640_release(fb);
            ESP_LOGW(TAG, "客户端断开");
            break;
        }
        camera_ov2640_release(fb);

        /* 控制推流速率约 10fps，减轻 Wi-Fi 压力 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

/* ==== /alarm 接口：电脑识别到跌倒后打过来 ==== */
static esp_err_t alarm_handler(httpd_req_t *req)
{
    s_fall_alarm = true;
    ESP_LOGW(TAG, "*** 收到上位机 FALL 报警指令 ***");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ==== 根页面：简单说明 ==== */
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<html><body><h2>SmartElderCare 网关</h2>"
        "<p>视频流: <a href='/stream'>/stream</a></p>"
        "<p>POST /alarm 触发本地声光报警</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t wifi_mjpeg_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t u_index  = { .uri = "/",       .method = HTTP_GET,  .handler = index_handler  };
    httpd_uri_t u_stream = { .uri = "/stream", .method = HTTP_GET,  .handler = stream_handler };
    httpd_uri_t u_alarm  = { .uri = "/alarm",  .method = HTTP_POST, .handler = alarm_handler  };
    httpd_register_uri_handler(server, &u_index);
    httpd_register_uri_handler(server, &u_stream);
    httpd_register_uri_handler(server, &u_alarm);

    ESP_LOGI(TAG, "HTTP 服务器已启动，端口 80");
    return ESP_OK;
}