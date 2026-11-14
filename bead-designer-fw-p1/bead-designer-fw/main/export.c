// Wi-Fi export: SoftAP + tiny HTTP server. The PHONE renders the PNG:
// /            -> embedded page (canvas) with Save-image button
// /grid.json   -> {"n":9,"brand":"Miyuki","palette":[{name,code,rgb}],"cells":[...]}
// Flat square pixels + colour legend + per-colour bead counts (spec decision).
// P1 TODO: qrcodegen (Nayuki, single C file) for the on-screen QR; random
// 8-char AP password shown beside it; captive-portal redirect (DNS) optional.
#include "export.h"
#include "ui.h"
#include "palettes.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "export";
static httpd_handle_t s_httpd;

static const char INDEX_HTML[] =
"<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Bead pattern</title><style>body{font-family:sans-serif;margin:16px}"
"canvas{width:100%;max-width:420px;image-rendering:pixelated;border:1px solid #ccc}"
"li{margin:4px 0}.sw{display:inline-block;width:14px;height:14px;margin-right:6px;"
"vertical-align:-2px;border:1px solid #0003}</style>"
"<h2>Bead pattern</h2><canvas id=c></canvas><p><button id=dl>Save image</button></p>"
"<h3>Beads</h3><ul id=leg></ul><script>"
"fetch('/grid.json').then(r=>r.json()).then(g=>{const S=64,c=document.getElementById('c');"
"c.width=c.height=g.n*S;const x=c.getContext('2d');const cnt={};"
"g.cells.forEach((v,i)=>{if(v<0){x.fillStyle='#fff'}else{x.fillStyle=g.palette[v].rgb;"
"cnt[v]=(cnt[v]||0)+1}x.fillRect((i%g.n)*S,((i/g.n)|0)*S,S,S)});"
"const ul=document.getElementById('leg');Object.keys(cnt).forEach(k=>{const p=g.palette[k];"
"ul.insertAdjacentHTML('beforeend',`<li><span class=sw style=background:${p.rgb}></span>"
"${p.name} (${p.code}) - ${cnt[k]}</li>`)});"
"document.getElementById('dl').onclick=()=>{const a=document.createElement('a');"
"a.download='bead-pattern.png';a.href=c.toDataURL('image/png');a.click()}})"
"</script>";

static esp_err_t h_index(httpd_req_t *r)
{ return httpd_resp_send(r, INDEX_HTML, HTTPD_RESP_USE_STRLEN); }

static esp_err_t h_grid(httpd_req_t *r)
{
    // Streamed in chunks: a full palette (254 entries, ~70 B each) is far larger
    // than any sensible static buffer. Each piece below is bounded by `buf`.
    const palette_t *p = &PALETTES[UI.palette];
    char buf[128];
    httpd_resp_set_type(r, "application/json");
    snprintf(buf, sizeof buf, "{\"n\":%d,\"brand\":\"%s\",\"palette\":[", GRID_N, p->brand);
    if (httpd_resp_sendstr_chunk(r, buf) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < p->count; i++) {
        uint16_t c = p->colors[i].rgb565;
        snprintf(buf, sizeof buf, "%s{\"name\":\"%s\",\"code\":\"%s\",\"rgb\":\"#%02x%02x%02x\"}",
                 i ? "," : "", p->colors[i].name, p->colors[i].code,
                 (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3);
        if (httpd_resp_sendstr_chunk(r, buf) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(r, "],\"cells\":[") != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < GRID_N * GRID_N; i++) {
        snprintf(buf, sizeof buf, "%s%d", i ? "," : "",
                 UI.cells[i] == CELL_EMPTY ? -1 : UI.cells[i]);
        if (httpd_resp_sendstr_chunk(r, buf) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(r, "]}") != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(r, NULL);   // end of chunked response
}

void export_start(void)
{
    // P1 TODO: move one-time esp_netif/wifi init to app_main; random password.
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wic);
    wifi_config_t ap = { .ap = {
        .ssid = "BeadPad",
        .password = "beads1234",
        .max_connection = 2,
        .authmode = WIFI_AUTH_WPA2_PSK,
    }};
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    httpd_start(&s_httpd, &hc);
    httpd_uri_t u1 = { .uri = "/", .method = HTTP_GET, .handler = h_index };
    httpd_uri_t u2 = { .uri = "/grid.json", .method = HTTP_GET, .handler = h_grid };
    httpd_register_uri_handler(s_httpd, &u1);
    httpd_register_uri_handler(s_httpd, &u2);
    ESP_LOGI(TAG, "export AP up: http://192.168.4.1/");
}

void export_stop(void)
{
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    esp_wifi_stop();
    ESP_LOGI(TAG, "export AP down");
}
