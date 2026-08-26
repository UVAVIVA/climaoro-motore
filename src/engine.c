#include "engine.h"
#include "devices.h"
#include "esph.h"
#include "climaoro.h"
#include "slugs.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "engine";

// Tempi di manutenzione centralizzata, come da climaoro.py.
#define RINNOVO_MS   (240ULL * 1000)
#define RIASSERT_MS  (1200ULL * 1000)

static bool s_master = false;
static int64_t s_last_rinnovo = 0;
static int64_t s_last_riassert = 0;
static char s_desired[DEV_MAX][8];
static bool s_desired_set[DEV_MAX];

static int64_t now_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

bool engine_master(void) { return s_master; }

void engine_set_master(bool on)
{
    s_master = on;
    nvs_handle_t h;
    if (nvs_open("motore", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "master", on ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "master=%d", on);
}

void engine_set_desired(int di, const char *mode)
{
    if (di < 0 || di >= DEVICES_N || !mode || !mode[0]) return;
    snprintf(s_desired[di], sizeof(s_desired[di]), "%s", mode);
    s_desired_set[di] = true;
}

// Rinnovo centralizzata + ri-assert modalita' (solo se master attivo).
static void centralizzata_tick(int64_t now)
{
    if (!s_master) return;

    if (now - s_last_rinnovo >= RINNOVO_MS) {
        for (int i = 0; i < DEVICES_N; i++) {
            const device_t *d = devices_get(i);
            if (!d->attivo || d->tipo != DEV_THERMOSTAT) continue;
            // ON garantito: un eventuale fallimento viene ritentato subito.
            bool ok = esph_switch_set(d->ip, SLUG_SWITCH, true);
            if (!ok) esph_switch_set(d->ip, SLUG_SWITCH, true);
            esph_button_press(d->ip, SLUG_RINNOVA);
        }
        s_last_rinnovo = now;
        ESP_LOGI(TAG, "centralizzata rinnovata");
    }

    if (now - s_last_riassert >= RIASSERT_MS) {
        for (int i = 0; i < DEVICES_N; i++) {
            const device_t *d = devices_get(i);
            if (!d->attivo || d->tipo != DEV_THERMOSTAT) continue;
            if (!s_desired_set[i]) continue;
            esph_climate_set(d->ip, SLUG_CLIMATE, s_desired[i], NULL, NULL);
        }
        s_last_riassert = now;
        ESP_LOGI(TAG, "modalita' ri-assertite");
    }
}

// Sintesi log dopo ogni poll: temp, umidita', mode/action del clima.
static void log_riassunto(const device_t *d, int i)
{
    dev_state_t *st = &g_state[i];
    if (!st->online) {
        ESP_LOGW(TAG, "[%s %s] OFFLINE", d->id, d->ip);
        return;
    }
    double t = esph_value(i, SLUG_SENSOR_TEMP, -273.15);
    double u = esph_value(i, SLUG_SENSOR_UMI, -1.0);
    const ent_t *c = esph_find(i, "climate-" SLUG_CLIMATE);
    ESP_LOGI(TAG, "[%s] T=%.1f U=%.0f%% mode=%s action=%s",
             d->id, t, u,
             c && c->mode[0] ? c->mode : "?",
             c && c->action[0] ? c->action : "?");
}

// Poll di tutti i dispositivi attivi (sequenziale: burst ~4-5 s l'uno).
static void poll_all(void)
{
    for (int i = 0; i < DEVICES_N; i++) {
        const device_t *d = devices_get(i);
        if (!d->attivo) continue;
        bool ok = esph_poll(i, d->ip);
        if (!ok) g_state[i].online = false;
        log_riassunto(d, i);
    }
}

static void decidi(void)
{
    cl_decidi();
}

static void engine_task(void *arg)
{
    // Al primo giro non aspettare un minuto intero.
    int64_t next_cycle = now_ms();
    for (;;) {
        int64_t now = now_ms();
        if (now >= next_cycle) {
            poll_all();
            decidi();
            next_cycle = now + 60000;
        }
        centralizzata_tick(now);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void engine_start(void)
{
    nvs_handle_t h;
    if (nvs_open("motore", NVS_READONLY, &h) == ESP_OK) {
        uint8_t m = 0;
        nvs_get_u8(h, "master", &m);
        s_master = m != 0;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "avvio motore (master=%d, %d dispositivi)",
             s_master, DEVICES_N);
    xTaskCreate(engine_task, "engine", 8192, NULL, 5, NULL);
}
