#include "web_portal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "web_portal";

#ifndef CONFIG_DRYING_WIFI_SSID
#define CONFIG_DRYING_WIFI_SSID ""
#endif
#ifndef CONFIG_DRYING_WIFI_PASSWORD
#define CONFIG_DRYING_WIFI_PASSWORD ""
#endif

#define COMMAND_QUEUE_LENGTH 4
#define WEB_TASK_STACK 6144
#define HTTP_TASK_STACK 6144

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"/>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>"
    "<title>自动晾晒收衣系统</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#0f172a;color:#e2e8f0}"
    "h1{font-size:20px;margin:0 0 12px}"
    ".card{background:#1e293b;border-radius:12px;padding:12px;margin-bottom:12px}"
    "dl{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin:0}"
    "dt{color:#94a3b8}"
    "button{flex:1;padding:10px;margin:4px;border:0;border-radius:8px;background:#334155;"
    "color:#e2e8f0;font-size:15px}button:active{background:#475569}"
    ".row{display:flex}</style></head><body><h1>自动晾晒收衣系统</h1>"
    "<div class=\"card\"><dl id=\"state\"><dt>状态</dt><dd>加载中…</dd></dl></div>"
    "<div class=\"card\"><div class=\"row\">"
    "<button onclick=\"cmd('retract')\">收回</button>"
    "<button onclick=\"cmd('extend')\">伸出</button></div><div class=\"row\">"
    "<button onclick=\"setMode('auto')\">自动模式</button>"
    "<button onclick=\"setMode('manual')\">手动模式</button></div></div><script>"
    "const P={unknown:'未知',extended:'已伸出',retracted:'已收回',moving_extend:'正在伸出',"
    "moving_retract:'正在收回'};const M={auto:'自动',manual:'手动'};"
    "function row(k,v){return '<dt>'+k+'</dt><dd>'+v+'</dd>';}"
    "async function refresh(){try{const r=await fetch('/api/status');const s=await r.json();"
    "const t=s.time_valid?s.time:'RTC 不可用';"
    "const tx=s.climate_valid?s.temperature_c.toFixed(1)+' °C':'--';"
    "const hm=s.climate_valid?s.humidity_percent.toFixed(1)+' %':'--';"
    "const lx=s.light_valid?s.lux.toFixed(0)+' lx':'--';"
    "const rn=s.rain_valid?(s.raining?'有雨':'无雨'):'--';"
    "document.getElementById('state').innerHTML=row('时间',t)+row('温湿度',tx+' / '+hm)+"
    "row('光照',lx)+row('雨滴',rn)+row('衣架',P[s.position]||s.position)+"
    "row('模式',M[s.mode]||s.mode)+row('Wi-Fi',s.wifi_connected?s.ip:'未连接');"
    "}catch(e){document.getElementById('state').innerHTML=row('状态','连接失败');}}"
    "async function cmd(a){await fetch('/api/'+a,{method:'POST'});refresh();}"
    "async function setMode(m){await fetch('/api/mode?value='+m,{method:'POST'});refresh();}"
    "refresh();setInterval(refresh,2000);</script></body></html>";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static web_status_t s_status;
static clothes_mode_t s_mode = CLOTHES_MODE_AUTO;
static QueueHandle_t s_commands;
static bool s_wifi_connected;
static char s_ip[16] = "0.0.0.0";
static httpd_handle_t s_server;

void web_portal_publish(const web_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_status = *status;
    s_status.mode = s_mode;
    s_status.wifi_connected = s_wifi_connected;
    memcpy(s_status.ip, s_ip, sizeof(s_status.ip));
    portEXIT_CRITICAL(&s_lock);
}

clothes_mode_t web_portal_mode(void)
{
    portENTER_CRITICAL(&s_lock);
    const clothes_mode_t mode = s_mode;
    portEXIT_CRITICAL(&s_lock);
    return mode;
}

bool web_portal_take_command(clothes_command_t *command)
{
    if (command == NULL || s_commands == NULL) {
        return false;
    }
    return xQueueReceive(s_commands, command, 0) == pdTRUE;
}

void web_portal_snapshot(web_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

static void set_mode(clothes_mode_t mode)
{
    portENTER_CRITICAL(&s_lock);
    s_mode = mode;
    s_status.mode = mode;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    web_status_t snapshot;
    portENTER_CRITICAL(&s_lock);
    snapshot = s_status;
    portEXIT_CRITICAL(&s_lock);
    char json[512];
    web_status_json(&snapshot, json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t json_error(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t queue_command(httpd_req_t *req, clothes_command_t command)
{
    if (s_commands == NULL || xQueueSend(s_commands, &command, 0) != pdTRUE) {
        return json_error(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"busy\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t retract_post(httpd_req_t *req)
{
    return queue_command(req, CLOTHES_CMD_RETRACT);
}

static esp_err_t extend_post(httpd_req_t *req)
{
    return queue_command(req, CLOTHES_CMD_EXTEND);
}

static esp_err_t mode_post(httpd_req_t *req)
{
    char query[32];
    char value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "value", value, sizeof(value)) != ESP_OK) {
        return json_error(req, "400 Bad Request", "{\"ok\":false,\"error\":\"value required\"}");
    }
    if (strcmp(value, "auto") == 0) {
        set_mode(CLOTHES_MODE_AUTO);
    } else if (strcmp(value, "manual") == 0) {
        set_mode(CLOTHES_MODE_MANUAL);
    } else {
        return json_error(req, "400 Bad Request", "{\"ok\":false,\"error\":\"auto|manual\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = HTTP_TASK_STACK;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/retract", .method = HTTP_POST, .handler = retract_post },
        { .uri = "/api/extend", .method = HTTP_POST, .handler = extend_post },
        { .uri = "/api/mode", .method = HTTP_POST, .handler = mode_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&s_lock);
        s_wifi_connected = false;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "Wi-Fi disconnected; retrying");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&s_lock);
        memcpy(s_ip, ip, sizeof(s_ip));
        s_wifi_connected = true;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "Web portal ready at http://%s/", ip);
    }
}

static esp_err_t start_wifi(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t config = {0};
    strncpy((char *)config.sta.ssid, CONFIG_DRYING_WIFI_SSID, sizeof(config.sta.ssid) - 1);
    strncpy((char *)config.sta.password, CONFIG_DRYING_WIFI_PASSWORD,
            sizeof(config.sta.password) - 1);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_start();
}

static void web_task(void *arg)
{
    (void)arg;
    esp_err_t err = start_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s; web portal disabled", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    err = start_http_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi STA %s; HTTP server started, waiting for an IP address",
             CONFIG_DRYING_WIFI_SSID);
    vTaskDelete(NULL);
}

esp_err_t web_portal_start(void)
{
    if (CONFIG_DRYING_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; web portal disabled (run idf.py menuconfig)");
        return ESP_ERR_INVALID_STATE;
    }
    s_commands = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(clothes_command_t));
    if (s_commands == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(web_task, "web_portal", WEB_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_commands);
        s_commands = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
