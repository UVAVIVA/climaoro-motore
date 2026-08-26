#include "esph.h"
#include "devices.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "esph";

dev_state_t *g_state = NULL;

void esph_init(void)
{
    g_state = heap_caps_calloc(DEV_MAX, sizeof(dev_state_t), MALLOC_CAP_SPIRAM);
    if (!g_state) {
        g_state = calloc(DEV_MAX, sizeof(dev_state_t));
    }
    if (!g_state) {
        ESP_LOGE(TAG, " impossibile allocare g_state, abort");
        abort();
    }
    ESP_LOGI(TAG, "g_state allocato (%u KB)",
             (unsigned)(DEV_MAX * sizeof(dev_state_t) / 1024));
}

// ---------------------------------------------------------------- utilita'

const ent_t *esph_find(int di, const char *ent_id)
{
    if (di < 0 || di >= DEV_MAX) return NULL;
    dev_state_t *st = &g_state[di];
    for (int i = 0; i < st->n; i++) {
        if (strcmp(st->e[i].id, ent_id) == 0) return &st->e[i];
    }
    return NULL;
}

double esph_value(int di, const char *ent_id, double fallback)
{
    const ent_t *e = esph_find(di, ent_id);
    return (e && e->has_value) ? e->value : fallback;
}

// Aggiorna (o aggiunge) un'entita' nella tabella del dispositivo.
static void store_json(int di, const cJSON *j)
{
    const cJSON *jid = cJSON_GetObjectItem(j, "id");
    if (!cJSON_IsString(jid) || !jid->valuestring[0]) return;

    dev_state_t *st = &g_state[di];
    ent_t *e = NULL;
    for (int i = 0; i < st->n; i++) {
        if (strcmp(st->e[i].id, jid->valuestring) == 0) { e = &st->e[i]; break; }
    }
    if (!e) {
        if (st->n >= 64) return;
        e = &st->e[st->n++];
        memset(e, 0, sizeof(*e));
        snprintf(e->id, sizeof(e->id), "%s", jid->valuestring);
    }

    const cJSON *nm  = cJSON_GetObjectItem(j, "name");
    const cJSON *stv = cJSON_GetObjectItem(j, "state");
    const cJSON *val = cJSON_GetObjectItem(j, "value");
    if (cJSON_IsString(nm) && nm->valuestring[0])
        snprintf(e->name, sizeof(e->name), "%s", nm->valuestring);
    if (cJSON_IsString(stv))
        snprintf(e->state, sizeof(e->state), "%s", stv->valuestring);
    if (cJSON_IsNumber(val)) { e->value = val->valuedouble; e->has_value = true; }

    // Campi specifici dell'entita' clima.
    const cJSON *md = cJSON_GetObjectItem(j, "mode");
    const cJSON *ac = cJSON_GetObjectItem(j, "action");
    if (cJSON_IsString(md))
        snprintf(e->mode, sizeof(e->mode), "%s", md->valuestring);
    if (cJSON_IsString(ac))
        snprintf(e->action, sizeof(e->action), "%s", ac->valuestring);
}

// ---------------------------------------------------------------- poll

// Legge /events per ~4 s: il web server invia subito tutte le entita'.
bool esph_poll(int di, const char *ip)
{
    char url[96];
    snprintf(url, sizeof(url), "http://%s/events", ip);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 5000,
        .buffer_size = 4096,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;

    bool ok = false;
    char buf[2048];
    char line[1024];
    int lp = 0;

    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(4500);
        for (;;) {
            int n = esp_http_client_read(c, buf, sizeof(buf));
            if (n <= 0) {
                if (xTaskGetTickCount() >= deadline) break;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            for (int i = 0; i < n; i++) {
                if (buf[i] == '\n' || lp >= (int)sizeof(line) - 1) {
                    line[lp] = '\0'; lp = 0;
                    if (strncmp(line, "data:", 5) == 0) {
                        cJSON *j = cJSON_Parse(line + 5);
                        if (j) {
                            store_json(di, j);
                            cJSON_Delete(j);
                            ok = true;
                        }
                    }
                } else if (buf[i] != '\r') {
                    line[lp++] = buf[i];
                }
            }
            if (xTaskGetTickCount() >= deadline) break;
        }
        esp_http_client_close(c);
    } else {
        ESP_LOGW(TAG, "%s: connessione fallita", ip);
    }
    esp_http_client_cleanup(c);

    if (ok) {
        g_state[di].online = true;
        g_state[di].last_ok_ms = (int64_t)(xTaskGetTickCount() / portTICK_PERIOD_MS);
    }
    return ok;
}

// ---------------------------------------------------------------- comandi

// POST con parametri in query, corpo vuoto: dialetto web_server.
// path_query esempio: "/climate/climatizzazione/set?mode=COOL"
static bool post(const char *ip, const char *path_query)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s%s", ip, path_query);

    for (int tentativo = 0; tentativo < 2; tentativo++) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 4000,
            .method = HTTP_METHOD_POST,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) return false;
        esp_http_client_set_header(c, "Content-Type",
                                   "application/x-www-form-urlencoded");
        esp_err_t err = esp_http_client_perform(c);
        int code = esp_http_client_get_status_code(c);
        esp_http_client_cleanup(c);
        if (err == ESP_OK && code >= 200 && code < 300) return true;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGW(TAG, "POST fallito: %s", url);
    return false;
}

bool esph_climate_set(const char *ip, const char *slug, const char *mode,
                      const double *low, const double *high)
{
    char q[192];
    q[0] = '\0';
    if (mode) snprintf(q + strlen(q), sizeof(q) - strlen(q), "mode=%s", mode);
    if (low)  snprintf(q + strlen(q), sizeof(q) - strlen(q),
                       "%starget_temperature_low=%.1f",
                       q[0] ? "&" : "", *low);
    if (high) snprintf(q + strlen(q), sizeof(q) - strlen(q),
                       "%starget_temperature_high=%.1f",
                       q[0] ? "&" : "", *high);
    char pq[256];
    snprintf(pq, sizeof(pq), "/climate/%s/set%s%s", slug, q[0] ? "?" : "", q);
    return post(ip, pq);
}

bool esph_number_set(const char *ip, const char *slug, double value)
{
    char pq[160];
    snprintf(pq, sizeof(pq), "/number/%s/set?value=%.1f", slug, value);
    return post(ip, pq);
}

bool esph_switch_set(const char *ip, const char *slug, bool on)
{
    char pq[160];
    snprintf(pq, sizeof(pq), "/switch/%s/turn_%s", slug, on ? "on" : "off");
    return post(ip, pq);
}

bool esph_button_press(const char *ip, const char *slug)
{
    char pq[160];
    snprintf(pq, sizeof(pq), "/button/%s/press", slug);
    return post(ip, pq);
}
