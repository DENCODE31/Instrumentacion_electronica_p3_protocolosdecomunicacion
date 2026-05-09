/*
 * Práctica 4 — Comunicación Inalámbrica: WiFi Básico
 * Instrumentación Electrónica · Universidad Nacional de Colombia
 *
 * Servidor HTTP con dashboard reactivo.
 * Control LED RGB (GPIO8 / WS2812) y lectura ADC canal 4 (GPIO4).
 */

#include <string.h>
#include <stdlib.h>


#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol_examples_common.h"
#include "led_strip.h"
#include "esp_adc/adc_oneshot.h"


static const char *TAG = "wifi_server";


#define LED_GPIO    8
#define ADC_CHANNEL ADC_CHANNEL_4


static led_strip_handle_t        s_led = NULL;
static adc_oneshot_unit_handle_t s_adc = NULL;
static volatile uint8_t          s_r = 0, s_g = 0, s_b = 0;
static volatile uint8_t          s_led_on     = 1;
static volatile uint8_t          s_brightness = 255;
static volatile int              s_adc_val    = 0;

/* ─── LED ────────────────────────────────────────────────────────────────── */

static void init_led(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num         = LED_GPIO,
        .max_leds               = 1,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led) != ESP_OK) {
        ESP_LOGE(TAG, "Error init LED strip");
        s_led = NULL;
        return;
    }
    led_strip_clear(s_led);
}

static void led_apply(void)
{
    if (!s_led) return;
    if (!s_led_on) {
        led_strip_clear(s_led);
        return;
    }
    uint8_t r = (uint8_t)((uint32_t)s_r * s_brightness / 255);
    uint8_t g = (uint8_t)((uint32_t)s_g * s_brightness / 255);
    uint8_t b = (uint8_t)((uint32_t)s_b * s_brightness / 255);
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
}

/* ─── ADC ────────────────────────────────────────────────────────────────── */

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "Error init ADC");
        s_adc = NULL;
        return;
    }
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL, &ch_cfg);
}

static void adc_brightness_task(void *arg)
{
    int raw;
    for (;;) {
        if (s_adc && adc_oneshot_read(s_adc, ADC_CHANNEL, &raw) == ESP_OK) {
            s_adc_val    = raw;
            s_brightness = (uint8_t)((uint32_t)raw * 255 / 4095);
            led_apply();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ─── Dashboard HTML ─────────────────────────────────────────────────────── */


static const char index_html[] =
    "<!doctype html><html lang='es'><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 - Practica 3</title>"
    "<style>"
    ":root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#e6edf3;"
    "--muted:#7d8590;--blue:#58a6ff;--green:#3fb950;--yellow:#d29922;--red:#f85149}"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:var(--bg);color:var(--text);"
    "font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
    "min-height:100vh;padding:20px;display:flex;justify-content:center}"
    ".wrap{width:min(700px,100%);padding-top:16px}"
    ".hdr{display:flex;align-items:center;justify-content:space-between;"
    "margin-bottom:24px;padding-bottom:16px;border-bottom:1px solid var(--border)}"
    ".brand{display:flex;align-items:center;gap:12px}"
    ".badge{background:var(--blue);color:#0d1117;font-weight:700;font-size:13px;"
    "padding:4px 10px;border-radius:6px;letter-spacing:.05em}"
    ".title{font-size:18px;font-weight:600}"
    ".sub{font-size:12px;color:var(--muted);margin-top:2px}"
    ".live{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--green)}"
    "@keyframes pulse{0%,100%{box-shadow:0 0 0 0 rgba(63,185,80,.5)}"
    "50%{box-shadow:0 0 0 7px rgba(63,185,80,0)}}"
    ".dot{width:8px;height:8px;border-radius:50%;background:var(--green);"
    "animation:pulse 2s ease infinite}"
    ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-bottom:20px}"
    "@media(min-width:500px){.grid{grid-template-columns:repeat(4,1fr)}}"
    ".card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px}"
    ".clabel{font-size:11px;text-transform:uppercase;letter-spacing:.08em;"
    "color:var(--muted);margin-bottom:10px}"
    ".cval{font-size:20px;font-weight:700;line-height:1;transition:color .3s}"
    ".csub{font-size:11px;color:var(--muted);margin-top:6px}"
    ".sect{background:var(--card);border:1px solid var(--border);border-radius:8px;"
    "padding:16px;margin-bottom:20px}"
    ".sect-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}"
    ".sect-title{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted)}"
    ".cprev{width:32px;height:32px;border-radius:6px;border:1px solid var(--border);background:#000}"
    ".pwr{padding:4px 14px;border-radius:6px;border:none;cursor:pointer;font-size:12px;"
    "font-weight:700;font-family:inherit;background:var(--green);color:#0d1117;"
    "transition:background .2s,color .2s}"
    ".led-wrap{display:flex;gap:12px;align-items:stretch}"
    ".bbar-wrap{display:flex;flex-direction:column;align-items:center;gap:4px;width:28px}"
    ".bbar-bg{flex:1;width:14px;border-radius:7px;background:#21262d;"
    "border:1px solid var(--border);position:relative;overflow:hidden;min-height:90px}"
    ".bbar-fill{position:absolute;bottom:0;left:0;right:0;background:#fff;"
    "transition:height .25s ease,background .25s ease;border-radius:7px}"
    ".bbar-lbl{font-size:10px;color:var(--muted);line-height:1}"
    ".led-ctrl{flex:1;min-width:0}"
    ".srow{display:flex;align-items:center;gap:10px;margin-bottom:8px}"
    ".srow:last-child{margin-bottom:0}"
    ".srow label{width:10px;font-size:13px;font-weight:700}"
    ".srow input[type=range]{flex:1;cursor:pointer}"
    ".srow span{width:28px;font-size:12px;text-align:right;color:var(--muted)}"
    ".adc-num{font-size:13px;color:var(--text);font-weight:600}"
    ".ch{width:100%;height:80px;display:block;border-radius:4px;"
    "background:var(--bg);border:1px solid var(--border)}"
    ".adc-ax{display:flex;justify-content:space-between;margin-top:6px;"
    "font-size:11px;color:var(--muted)}"
    ".ftr{text-align:center;font-size:12px;color:var(--muted);padding-bottom:20px}"
    "@keyframes flash{0%,100%{opacity:1}50%{opacity:.25}}"
    ".flash{animation:flash .35s ease}"
    "</style></head><body>"
    "<div class='wrap'>"
    "<div class='hdr'>"
    "<div class='brand'>"
    "<span class='badge'>ESP32-C6</span>"
    "<div>"
    "<div class='title'>Practica 3 - WiFi Basico</div>"
    "<div class='sub'>Instrumentacion Electronica - UNAL</div>"
    "</div>"
    "</div>"
    "<div class='live'><span class='dot'></span>Online</div>"
    "</div>"
    "<div class='grid'>"
    "<div class='card'>"
    "<div class='clabel'>Uptime</div>"
    "<div class='cval' id='up'>--:--:--</div>"
    "<div class='csub'>hh:mm:ss</div>"
    "</div>"
    "<div class='card'>"
    "<div class='clabel'>Heap libre</div>"
    "<div class='cval' id='hp'>--- KB</div>"
    "<div class='csub' id='hp2'>···</div>"
    "</div>"
    "<div class='card'>"
    "<div class='clabel'>WiFi RSSI</div>"
    "<div class='cval' id='rs'>--- dBm</div>"
    "<div class='csub' id='rs2'>···</div>"
    "</div>"
    "<div class='card'>"
    "<div class='clabel'>IP Local</div>"
    "<div class='cval' id='ip' style='font-size:14px'>···</div>"
    "<div class='csub'>IPv4</div>"
    "</div>"
    "</div>"
    "<div class='sect'>"
    "<div class='led-wrap'>"
    "<div class='bbar-wrap'>"
    "<span class='bbar-lbl' id='bval'>---%</span>"
    "<div class='bbar-bg'>"
    "<div class='bbar-fill' id='bfill' style='height:0%'></div>"
    "</div>"
    "<span class='bbar-lbl'>0%</span>"
    "</div>"
    "<div class='led-ctrl'>"
    "<div class='sect-hdr'>"
    "<span class='sect-title'>Control LED RGB</span>"
    "<div style='display:flex;align-items:center;gap:8px'>"
    "<div class='cprev' id='cprev'></div>"
    "<button class='pwr' id='pwr'>ON</button>"
    "</div>"
    "</div>"
    "<div class='srow'>"
    "<label style='color:var(--red)'>R</label>"
    "<input type='range' id='sr' min='0' max='255' value='0'>"
    "<span id='vr'>0</span>"
    "</div>"
    "<div class='srow'>"
    "<label style='color:var(--green)'>G</label>"
    "<input type='range' id='sg' min='0' max='255' value='0'>"
    "<span id='vg'>0</span>"
    "</div>"
    "<div class='srow'>"
    "<label style='color:var(--blue)'>B</label>"
    "<input type='range' id='sb' min='0' max='255' value='0'>"
    "<span id='vb'>0</span>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='sect'>"
    "<div class='sect-hdr'>"
    "<span class='sect-title'>ADC Canal 4 — Potenciometro</span>"
    "<span class='adc-num' id='av'>--- / 4095</span>"
    "</div>"
    "<canvas id='ch' class='ch' height='80'></canvas>"
    "<div class='adc-ax'>"
    "<span>0.00 V</span>"
    "<span id='vv'>- V</span>"
    "<span>3.30 V</span>"
    "</div>"
    "</div>"
    "<div class='ftr'>Universidad Nacional de Colombia - Ingenieria Electronica - 2026</div>"
    "</div>"
    "<script>"
    "(function(){"
    "var $=function(id){return document.getElementById(id);};"
    "function fmt(s){"
    "var h=Math.floor(s/3600),m=Math.floor(s%3600/60),sec=s%60;"
    "return [h,m,sec].map(function(x){return String(x).padStart(2,'0');}).join(':');"
    "}"
    "function rColor(r){"
    "return r>=-55?'var(--green)':r>=-70?'var(--blue)':r>=-80?'var(--yellow)':'var(--red)';"
    "}"
    "function rLabel(r){"
    "return r>=-55?'Excelente':r>=-70?'Bueno':r>=-80?'Regular':'Debil';"
    "}"
    "function flash(id){"
    "var e=$(id);if(!e)return;"
    "e.classList.remove('flash');void e.offsetWidth;e.classList.add('flash');"
    "}"
    "var adcBuf=new Array(60).fill(0);"
    "var cvs=$('ch'),ctx=cvs.getContext('2d');"
    "function drawAdc(){"
    "var w,h,i,j,x,y;"
    "cvs.width=cvs.offsetWidth||640;"
    "w=cvs.width;h=cvs.height;"
    "ctx.fillStyle='#0d1117';"
    "ctx.fillRect(0,0,w,h);"
    "ctx.strokeStyle='#21262d';"
    "ctx.lineWidth=1;"
    "for(i=1;i<4;i++){"
    "y=Math.round(h*i/4)+.5;"
    "ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();"
    "}"
    "ctx.strokeStyle='#58a6ff';"
    "ctx.lineWidth=1.5;"
    "ctx.beginPath();"
    "for(j=0;j<adcBuf.length;j++){"
    "x=j/(adcBuf.length-1)*w;"
    "y=h-(adcBuf[j]/4095)*(h-4)-2;"
    "j===0?ctx.moveTo(x,y):ctx.lineTo(x,y);"
    "}"
    "ctx.stroke();"
    "}"
    "function poll(){"
    "fetch('/api/status')"
    ".then(function(r){return r.json();})"
    ".then(function(d){"
    "$('up').textContent=fmt(d.uptime);"
    "var kb=Math.round(d.heap/1024);"
    "$('hp').textContent=kb+' KB';"
    "$('hp').style.color=kb>150?'var(--green)':kb>80?'var(--yellow)':'var(--red)';"
    "$('hp2').textContent='heap libre';"
    "var rsEl=$('rs');"
    "rsEl.textContent=d.rssi+' dBm';"
    "rsEl.style.color=rColor(d.rssi);"
    "$('rs2').textContent=rLabel(d.rssi);"
    "$('ip').textContent=d.ip||'N/A';"
    "var raw=d.adc||0;"
    "adcBuf.push(raw);adcBuf.shift();"
    "$('av').textContent=raw+' / 4095';"
    "$('vv').textContent=(raw*3.3/4095).toFixed(2)+' V';"
    "drawAdc();"
    "['up','hp','rs'].forEach(flash);"
    "if(typeof d.led_on!=='undefined'&&d.led_on!==ledOn){ledOn=d.led_on;applyPwrBtn();}"
    "var bpct=typeof d.brightness!=='undefined'?d.brightness:Math.round((d.adc||0)*100/4095);"
    "$('bfill').style.height=bpct+'%';"
    "$('bval').textContent=bpct+'%';"
    "})"
    ".catch(function(){"
    "var dot=document.querySelector('.dot');"
    "if(dot)dot.style.background='var(--red)';"
    "});"
    "}"
    "setInterval(poll,1000);"
    "poll();"
    "var ledOn=true;"
    "function applyPwrBtn(){"
    "var b=$('pwr');"
    "b.textContent=ledOn?'ON':'OFF';"
    "b.style.background=ledOn?'var(--green)':'var(--red)';"
    "b.style.color=ledOn?'#0d1117':'#fff';"
    "}"
    "function togglePwr(){"
    "ledOn=!ledOn;"
    "applyPwrBtn();"
    "fetch('/led/power',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({on:ledOn})"
    "});"
    "}"
    "var lt=null;"
    "function syncLed(){"
    "var r=+$('sr').value,g=+$('sg').value,b=+$('sb').value;"
    "$('vr').textContent=r;"
    "$('vg').textContent=g;"
    "$('vb').textContent=b;"
    "$('cprev').style.background='rgb('+r+','+g+','+b+')';"
    "$('bfill').style.background='rgb('+r+','+g+','+b+')';"
    "clearTimeout(lt);"
    "lt=setTimeout(function(){"
    "fetch('/led',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({r:r,g:g,b:b})"
    "});"
    "},80);"
    "}"
    "$('pwr').onclick=togglePwr;"
    "$('sr').oninput=$('sg').oninput=$('sb').oninput=syncLed;"
    "})();"
    "</script></body></html>";

/* ─── Handlers HTTP ──────────────────────────────────────────────────────── */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t uri_index = {
    .uri     = "/",
    .method  = HTTP_GET,
    .handler = index_get_handler,
};

/* ─────────────────────────────────────────────────────────────────────────── */

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_favicon = {
    .uri     = "/favicon.ico",
    .method  = HTTP_GET,
    .handler = favicon_get_handler,
};

/* ─────────────────────────────────────────────────────────────────────────── */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char buf[224];

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    char ip_str[16] = "0.0.0.0";
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    wifi_ap_record_t ap_info = {0};
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    int64_t  uptime_s  = esp_timer_get_time() / 1000000LL;
    uint32_t free_heap = esp_get_free_heap_size();

    int adc_val = s_adc_val;

    snprintf(buf, sizeof(buf),
        "{\"uptime\":%lld,\"heap\":%lu,\"rssi\":%d,\"ip\":\"%s\","
        "\"adc\":%d,\"led_on\":%s,\"brightness\":%u}",
        uptime_s, (unsigned long)free_heap, rssi, ip_str,
        adc_val, s_led_on ? "true" : "false",
        (unsigned)((uint32_t)s_brightness * 100 / 255));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t uri_status = {
    .uri     = "/api/status",
    .method  = HTTP_GET,
    .handler = status_get_handler,
};

/* ─────────────────────────────────────────────────────────────────────────── */

static esp_err_t echo_post_handler(httpd_req_t *req)
{
    char buf[128];
    int  remaining = req->content_len;
    int  ret;

    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, MIN(remaining, (int)sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_echo = {
    .uri     = "/echo",
    .method  = HTTP_POST,
    .handler = echo_post_handler,
};

/* ─────────────────────────────────────────────────────────────────────────── */

static esp_err_t led_post_handler(httpd_req_t *req)
{
    char buf[64];
    int  len = MIN(req->content_len, (int)sizeof(buf) - 1);

    if (len <= 0 || httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    unsigned int r = 0, g = 0, b = 0;
    char *pr = strstr(buf, "\"r\":");
    char *pg = strstr(buf, "\"g\":");
    char *pb = strstr(buf, "\"b\":");
    if (pr) r = (unsigned int)strtoul(pr + 4, NULL, 10);
    if (pg) g = (unsigned int)strtoul(pg + 4, NULL, 10);
    if (pb) b = (unsigned int)strtoul(pb + 4, NULL, 10);
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;

    s_r = (uint8_t)r;
    s_g = (uint8_t)g;
    s_b = (uint8_t)b;

    led_apply();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t uri_led = {
    .uri     = "/led",
    .method  = HTTP_POST,
    .handler = led_post_handler,
};

static esp_err_t led_power_post_handler(httpd_req_t *req)
{
    char buf[32];
    int  len = MIN(req->content_len, (int)sizeof(buf) - 1);

    if (len <= 0 || httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char *pon = strstr(buf, "\"on\":");
    if (pon) {
        pon += 5;
        while (*pon == ' ') pon++;
        s_led_on = (strncmp(pon, "true", 4) == 0) ? 1 : 0;
    }

    led_apply();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t uri_led_power = {
    .uri     = "/led/power",
    .method  = HTTP_POST,
    .handler = led_power_post_handler,
};

/* ─── Ciclo de vida del servidor ─────────────────────────────────────────── */

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor en puerto %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Error al iniciar el servidor");
        return NULL;
    }

    httpd_register_uri_handler(server, &uri_index);
    httpd_register_uri_handler(server, &uri_favicon);
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_echo);
    httpd_register_uri_handler(server, &uri_led);
    httpd_register_uri_handler(server, &uri_led_power);

    ESP_LOGI(TAG, "Servidor listo — abre http://<ip> en tu navegador");
    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}

static void connect_handler(void *arg, esp_event_base_t base,
                            int32_t id, void *data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL) {
        *server = start_webserver();
    }
}

static void disconnect_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server) {
        ESP_LOGI(TAG, "WiFi desconectado — deteniendo servidor");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        }
    }
}

/* ─── Punto de entrada ───────────────────────────────────────────────────── */

void app_main(void)
{
    static httpd_handle_t server = NULL;

    init_led();
    init_adc();
    xTaskCreate(adc_brightness_task, "adc_bright", 2048, NULL, 5, NULL);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP,        &connect_handler,    &server));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    server = start_webserver();

    while (server) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
