#include "climaoro.h"
#include "esph.h"
#include "devices.h"
#include "engine.h"
#include "slugs.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

static const char *TAG = "climaoro";

cl_config_t g_cfg;

// ---------------------------------------------------------------- NVS

static const char *NVS_NS = "climaoro";

void cl_config_init(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS vuoto: config default");
        return;
    }

    // Legacy: prova blob singolo (compatibilita')
    size_t sz = sizeof(g_cfg);
    if (nvs_get_blob(h, "cfg", &g_cfg, &sz) == ESP_OK) {
        ESP_LOGI(TAG, "NVS: config caricata (blob singolo, %d bytes)", (int)sz);
        nvs_close(h);
        return;
    }

    // Nuovo formato: entry separate
    uint8_t n_ap = 0, n_stz = 0;
    nvs_get_u8(h, "n_ap", &n_ap);
    nvs_get_u8(h, "n_stz", &n_stz);
    g_cfg.n_apartamenti = n_ap;
    g_cfg.n_stanze = n_stz;

    for (int i = 0; i < n_ap && i < CL_AP_MAX; i++) {
        char key[8];
        snprintf(key, sizeof(key), "ap%d", i);
        sz = sizeof(cl_apartamento_t);
        nvs_get_blob(h, key, &g_cfg.ap[i], &sz);
    }

    {
        sz = sizeof(cl_stanza_t) * CL_STZ_MAX * CL_GRP_MAX * CL_AP_MAX;
        nvs_get_blob(h, "stz", g_cfg.stanze, &sz);
    }

    ESP_LOGI(TAG, "NVS: config caricata (%d ap, %d stanze)", g_cfg.n_apartamenti, g_cfg.n_stanze);
    nvs_close(h);
}

void cl_config_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, "n_ap", (uint8_t)g_cfg.n_apartamenti);
    nvs_set_u8(h, "n_stz", (uint8_t)g_cfg.n_stanze);

    for (int i = 0; i < g_cfg.n_apartamenti && i < CL_AP_MAX; i++) {
        char key[8];
        snprintf(key, sizeof(key), "ap%d", i);
        nvs_set_blob(h, key, &g_cfg.ap[i], sizeof(cl_apartamento_t));
    }

    nvs_set_blob(h, "stz", g_cfg.stanze,
                 sizeof(cl_stanza_t) * CL_STZ_MAX * CL_GRP_MAX * CL_AP_MAX);

    // Rimuovi vecchio blob singolo se presente
    nvs_erase_key(h, "cfg");

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "config salvata in NVS (%d ap, %d stanze)",
             g_cfg.n_apartamenti, g_cfg.n_stanze);
}

// ---------------------------------------------------------------- helper

// Assicura che la modalita' centralizzata sia attiva sul termostato.
// Retry x2 come _comando_sicuro_per_stanza() di AppDaemon.
// Ritorna true se la centralizzata e' ON (o se non esiste l'entita').
static bool assicura_centralizzata(int di)
{
    const device_t *d = devices_get(di);
    if (!d) return false;

    for (int tentativo = 0; tentativo < 2; tentativo++) {
        const ent_t *sw = esph_find(di, "switch-" SLUG_SWITCH);
        if (!sw) {
            // Entita' non trovata: probabilmente non ha centralizzata.
            return true;
        }
        if (sw->state[0] && strcasecmp(sw->state, "on") == 0) {
            return true;
        }
        // Centralizzata spenta: accendila.
        ESP_LOGW(TAG, "[%s] centralizzata spenta, attivazione (tentativo %d)",
                 d->id, tentativo + 1);
        esph_switch_set(d->ip, SLUG_SWITCH, true);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Ultimo controllo dopo il secondo tentativo.
    const ent_t *sw = esph_find(di, "switch-" SLUG_SWITCH);
    if (sw && sw->state[0] && strcasecmp(sw->state, "on") == 0) {
        return true;
    }

    ESP_LOGE(TAG, "[%s] ERRORE: centralizzata non attivabile", d->id);
    return false;
}

int fascia_ora(int ora, const uint8_t calendario[CL_GG][CL_ORA])
{
    time_t now_t;
    time(&now_t);
    struct tm ti;
    localtime_r(&now_t, &ti);
    int gg = ti.tm_wday; // 0=dom
    // Nostra indicizzazione: 0=lun -> conversione: lun=0, dom=6
    gg = (gg + 6) % 7;
    int o = ti.tm_hour;
    if (o < 0 || o >= CL_ORA) return CL_ECO;
    return calendario[gg][o];
}

// ---------------------------------------------------------------- decidi

void cl_decidi(void)
{
    if (!engine_master()) {
        ESP_LOGD(TAG, "master OFF, skip ciclo");
        return;
    }

    for (int ai = 0; ai < g_cfg.n_apartamenti; ai++) {
        cl_apartamento_t *ap = &g_cfg.ap[ai];
        if (!ap->attivo) continue;

        for (int gi = 0; gi < ap->n_gruppi; gi++) {
            cl_gruppo_t *gr = &ap->gruppi[gi];

            int fascia = fascia_ora(0, gr->calendario);
            if (fascia == CL_AUTONOMO) {
                ESP_LOGD(TAG, "[ap%d g%d] autonomo: skip", ai, gi);
                continue;
            }

            float delta_acc = (fascia == CL_COMFORT) ? gr->delta_acc_comfort : gr->delta_acc_eco;
            float delta_sp  = (fascia == CL_COMFORT) ? gr->delta_sp_comfort  : gr->delta_sp_eco;

            // Raccoglie informazioni per ogni stanza del gruppo.
            int n_ok = 0;
            float totale_pesi = 0.0f;
            bool qualche_attivo = false;

            for (int si = 0; si < gr->n_stanze; si++) {
                int sii = gr->stanze[si];
                cl_stanza_t *st = &g_cfg.stanze[sii];

                // Trova il dispositivo corrispondente per id.
                int di = -1;
                for (int d = 0; d < DEVICES_N; d++) {
                    const device_t *dev = devices_get(d);
                    if (dev && strcmp(dev->id, st->id) == 0) { di = d; break; }
                }
                if (di < 0 || !g_state[di].online) {
                    ESP_LOGW(TAG, "stanza %s: offline o non trovata", st->id);
                    continue;
                }

                if (!st->inclusa) {
                    ESP_LOGD(TAG, "stanza %s: esclusa", st->id);
                    continue;
                }

                // Legge stato corrente dal termostato.
                const ent_t *cl = esph_find(di, "climate-" SLUG_CLIMATE);
                if (!cl) {
                    ESP_LOGW(TAG, "stanza %s: entita' clima mancante", st->id);
                    continue;
                }
                float temp_reale = esph_value(di, SLUG_SENSOR_TEMP, -273.15f);
                if (temp_reale < -100.0f) {
                    ESP_LOGW(TAG, "stanza %s: temp_reale non disponibile", st->id);
                    continue;
                }
                const ent_t *e_ts = esph_find(di, SLUG_SENSOR_TSAL);
                if (!e_ts || !e_ts->has_value) {
                    ESP_LOGW(TAG, "stanza %s: temp_salvata non disponibile, skip", st->id);
                    continue;
                }
                float temp_salvata = (float)e_ts->value;

                // Accende: temp_salvata + delta_acc
                float sp_acc = temp_salvata + delta_acc;
                // Spegne: temp_salvata + delta_sp
                float sp_sp  = temp_salvata + delta_sp;
                // Richiesta: temp_salvata - 0.5
                float soglia_richiesta = temp_salvata - 0.5f;

                ESP_LOGI(TAG, "[%s] T=%.1f sal=%.1f acc=%.1f sp=%.1f rich=%.1f act=%s",
                         st->id, temp_reale, temp_salvata, sp_acc, sp_sp,
                         soglia_richiesta, cl->action);

                // Se sta gia' scaldando: ha priorita', non toccare.
                if (strcasecmp(cl->action, "heating") == 0) {
                    qualche_attivo = true;
                    n_ok++;
                    ESP_LOGI(TAG, "[%s] in riscaldamento: priorita'", st->id);
                    continue;
                }

                // Allinea setpoint all'accensione (scrive temp_salvata + delta_acc).
                // Prima verifica che la modalita' centralizzata sia attiva.
                if (!assicura_centralizzata(di)) {
                    ESP_LOGW(TAG, "[%s] centralizzata non attiva, skip", st->id);
                    continue;
                }
                esph_number_set(devices_get(di)->ip, SLUG_TEMP_SAL, (double)sp_acc);

                // Se la temperatura e' sotto la soglia di richiesta, la stanza chiede.
                if (temp_reale <= soglia_richiesta) {
                    totale_pesi += st->peso;
                    ESP_LOGI(TAG, "[%s] richiesta (peso=%.1f tot=%.1f)", st->id, st->peso, totale_pesi);
                }
                n_ok++;
            }

            if (n_ok == 0) continue;

            // Decisione: accende le stanze che hanno fatto richiesta.
            bool accendi = false;
            if (qualche_attivo) {
                ESP_LOGI(TAG, "priorita': zone attive -> accendo richiedenti");
                accendi = true;
            } else if (totale_pesi >= ap->soglia_pesi) {
                ESP_LOGI(TAG, "soglia raggiunta (%.1f >= %.1f)", totale_pesi, ap->soglia_pesi);
                accendi = true;
            } else {
                // Emergenza individuale (sicurezza 0.4): nessuna priorita' ne'
                // soglia pesi raggiunta, ma la stanza e' molto sotto la soglia di
                // accensione (temp <= temp_salvata + delta_acc - 0.4): si accende
                // da sola (replica appdaemon).
                ESP_LOGI(TAG, "emergenza individuale");
                for (int si = 0; si < gr->n_stanze; si++) {
                    int sii = gr->stanze[si];
                    cl_stanza_t *st = &g_cfg.stanze[sii];

                    int di = -1;
                    for (int d = 0; d < DEVICES_N; d++) {
                        const device_t *dev = devices_get(d);
                        if (dev && strcmp(dev->id, st->id) == 0) { di = d; break; }
                    }
                    if (di < 0 || !g_state[di].online || !st->inclusa) continue;

                    float temp_reale = esph_value(di, SLUG_SENSOR_TEMP, -273.15f);
                    const ent_t *e_ts3 = esph_find(di, SLUG_SENSOR_TSAL);
                    if (!e_ts3 || !e_ts3->has_value) {
                        ESP_LOGW(TAG, "[%s] temp_salvata non disponibile, skip emergenza", st->id);
                        continue;
                    }
                    float temp_salvata = (float)e_ts3->value;
                    float sp_sp = temp_salvata + delta_sp;

                    float soglia_emergenza = temp_salvata + delta_acc - 0.4f;
                    if (temp_reale > soglia_emergenza) {
                        ESP_LOGD(TAG, "[%s] sotto soglia emergenza (%.1f > %.1f): skip",
                                 st->id, temp_reale, soglia_emergenza);
                        continue;
                    }

                    if (!assicura_centralizzata(di)) {
                        ESP_LOGW(TAG, "[%s] centralizzata non attiva, skip emergenza", st->id);
                        continue;
                    }
                    ESP_LOGI(TAG, "[%s] EMERGENZA: accendo da solo (%.1f <= %.1f)",
                             st->id, temp_reale, soglia_emergenza);
                    esph_number_set(devices_get(di)->ip, SLUG_TEMP_SAL, (double)sp_sp);
                }
            }

            if (accendi) {
                for (int si = 0; si < gr->n_stanze; si++) {
                    int sii = gr->stanze[si];
                    cl_stanza_t *st = &g_cfg.stanze[sii];
                    int di = -1;
                    for (int d = 0; d < DEVICES_N; d++) {
                        const device_t *dev = devices_get(d);
                        if (dev && strcmp(dev->id, st->id) == 0) { di = d; break; }
                    }
                    if (di < 0 || !g_state[di].online || !st->inclusa) continue;

                    float temp_reale = esph_value(di, SLUG_SENSOR_TEMP, -273.15f);
                    const ent_t *e_ts2 = esph_find(di, SLUG_SENSOR_TSAL);
                    if (!e_ts2 || !e_ts2->has_value) {
                        ESP_LOGW(TAG, "[%s] temp_salvata non disponibile, skip accensione", st->id);
                        continue;
                    }
                    float temp_salvata = (float)e_ts2->value;
                    float soglia_richiesta = temp_salvata - 0.5f;

                    if (temp_reale <= soglia_richiesta) {
                        float sp_sp = temp_salvata + delta_sp;
                        // Prima verifica centralizzata attiva.
                        if (!assicura_centralizzata(di)) {
                            ESP_LOGW(TAG, "[%s] centralizzata non attiva, skip accensione", st->id);
                            continue;
                        }
                        ESP_LOGI(TAG, "[%s] ACCENDO: setpoint -> %.1f", st->id, sp_sp);
                        esph_number_set(devices_get(di)->ip, SLUG_TEMP_SAL, (double)sp_sp);
                    }
                }
            }
        }
    }
}
