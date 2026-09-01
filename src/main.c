#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "secrets.h"
#include "devices.h"
#include "esph.h"
#include "engine.h"
#include "climaoro.h"
#include "led.h"

static const char *TAG = "motore";

// --- Hook cJSON -> PSRAM per evitare frammentazione SRAM ---
// Se la PSRAM non e' disponibile a runtime, ripiega su SRAM.
static void *cjson_psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(sz);
}
static void  cjson_psram_free(void *p)         { heap_caps_free(p); }

static EventGroupHandle_t s_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static httpd_handle_t s_server = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        ESP_LOGW(TAG, "wifi disconnesso, tentativo %d", s_retry_num);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "ip assegnato: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID1,
            .password = WIFI_PASS1,
        },
    };
    memcpy(wifi_config.sta.ssid, WIFI_SSID1, strlen(WIFI_SSID1));
    memcpy(wifi_config.sta.password, WIFI_PASS1, strlen(WIFI_PASS1));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "attesa connessione wifi...");
    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_dhcpc_stop(netif);

        esp_netif_ip_info_t info;
        memset(&info, 0, sizeof(info));
        info.ip.addr = esp_ip4addr_aton(MOTORE_IP);
        info.gw.addr = esp_ip4addr_aton(MOTORE_GATEWAY);
        info.netmask.addr = esp_ip4addr_aton(MOTORE_NETMASK);
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &info));
        ESP_LOGI(TAG, "ip statico configurato: %s", MOTORE_IP);

        // DNS mancati: senza DHCP, servono altrimenti i nomi host non vengono risolti
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(MOTORE_GATEWAY);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
        ESP_LOGI(TAG, "DNS configurati: main=%s backup=8.8.8.8", MOTORE_GATEWAY);

        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        // SNTP con API moderna esp_netif_sntp
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(4,
            ESP_SNTP_SERVER_LIST(
                MOTORE_GATEWAY,
                "216.239.35.12",
                "216.239.35.0",
                "129.6.15.28"
            ));
        esp_err_t sntp_err = esp_netif_sntp_init(&sntp_cfg);
        ESP_LOGI(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(sntp_err));
        // Attende sync SNTP con timeout 10s
        if (sntp_err == ESP_OK) {
            esp_err_t sync_err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
            if (sync_err == ESP_OK) {
                time_t now;
                time(&now);
                struct tm ti;
                localtime_r(&now, &ti);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
                ESP_LOGI(TAG, "sntp sync OK: %s", buf);
            } else {
                ESP_LOGW(TAG, "sntp sync fallito (%s), riprovera in background", esp_err_to_name(sync_err));
            }
        }
    } else {
        ESP_LOGE(TAG, "connessione wifi fallita");
    }
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nome", "climaoro-motore");
    cJSON_AddStringToObject(root, "ip", MOTORE_IP);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(xTaskGetTickCount() / configTICK_RATE_HZ));
    cJSON_AddBoolToObject(root, "sntp_sync", time(NULL) > 1000000000);
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    cJSON_AddStringToObject(root, "ora_locale", buf);
    cJSON_AddBoolToObject(root, "master", engine_master());

    cJSON *devs = cJSON_AddArrayToObject(root, "dispositivi");
    for (int i = 0; i < DEVICES_N; i++) {
        const device_t *d = devices_get(i);
        if (!d->attivo) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", d->id);
        cJSON_AddStringToObject(o, "nome", d->nome);
        cJSON_AddStringToObject(o, "ip", d->ip);
        cJSON_AddBoolToObject(o, "online", g_state[i].online);
        double t = esph_value(i, "sensor-temperatura_reale", -273.15);
        double u = esph_value(i, "sensor-umidit___reale", -1.0);
        if (t > -100.0) cJSON_AddNumberToObject(o, "temperatura", t);
        if (u >= 0.0)   cJSON_AddNumberToObject(o, "umidita", u);
        const ent_t *c = esph_find(i, "climate-climatizzazione");
        if (c) {
            if (c->mode[0])   cJSON_AddStringToObject(o, "mode", c->mode);
            if (c->action[0]) cJSON_AddStringToObject(o, "action", c->action);
        }
        cJSON_AddItemToArray(devs, o);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = status_handler,
};

// --- Devices API: /api/devices ---

static esp_err_t devices_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < DEVICES_N; i++) {
        const device_t *d = devices_get(i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", d->id);
        cJSON_AddStringToObject(o, "nome", d->nome);
        cJSON_AddStringToObject(o, "ip", d->ip);
        cJSON_AddStringToObject(o, "tipo", d->tipo == DEV_THERMOSTAT ? "termostato" : "collettore");
        cJSON_AddBoolToObject(o, "attivo", d->attivo);
        cJSON_AddBoolToObject(o, "online", g_state[i].online);
        cJSON_AddItemToArray(root, o);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_devices = {
    .uri = "/api/devices",
    .method = HTTP_GET,
    .handler = devices_handler,
};

// POST /api/devices: riceve la lista dei termostati dall'app
// (nessun dispositivo hardcoded), la salva in NVS e la usa per poll/comandi.
static esp_err_t devices_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "payload vuoto o troppo grande");
        return ESP_FAIL;
    }
    char *buf = calloc(1, total + 1);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    // Legge il body a pezzi finche' non ha ricevuto tutti i content_len byte:
    // httpd_req_recv puo' restituire il body in piu' chunk, e gestirlo
    // come singola lettura lascia la connessione bloccata (timeout /
    // connection reset) con l'app che resta in attesa.
    int off = 0;
    while (off < total) {
        int rec = httpd_req_recv(req, buf + off, total - off);
        if (rec <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "short read");
            return ESP_FAIL;
        }
        off += rec;
    }
    buf[off] = '\0';

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json invalido");
        return ESP_FAIL;
    }

    device_t *list = calloc(DEV_MAX, sizeof(device_t));
    if (!list) {
        cJSON_Delete(arr);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int n = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (n >= DEV_MAX) break;
        cJSON *jid  = cJSON_GetObjectItem(item, "id");
        cJSON *jnom = cJSON_GetObjectItem(item, "nome");
        cJSON *jip  = cJSON_GetObjectItem(item, "ip");
        if (!cJSON_IsString(jid) || !cJSON_IsString(jnom) || !cJSON_IsString(jip)) continue;
        snprintf(list[n].id,   sizeof(list[n].id),   "%s", jid->valuestring);
        snprintf(list[n].nome, sizeof(list[n].nome), "%s", jnom->valuestring);
        snprintf(list[n].ip,   sizeof(list[n].ip),   "%s", jip->valuestring);
        cJSON *jt = cJSON_GetObjectItem(item, "tipo");
        list[n].tipo = (cJSON_IsString(jt) && strcmp(jt->valuestring, "collettore") == 0)
                           ? DEV_COLLECTOR : DEV_THERMOSTAT;
        cJSON *ja = cJSON_GetObjectItem(item, "attivo");
        list[n].attivo = !ja || cJSON_IsTrue(ja);
        n++;
    }
    cJSON_Delete(arr);

    n = devices_set(list, n);
    free(list);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddBoolToObject(res, "ok", true);
    cJSON_AddNumberToObject(res, "count", n);
    const char *json = cJSON_PrintUnformatted(res);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(res);
    return ESP_OK;
}

static const httpd_uri_t uri_devices_post = {
    .uri = "/api/devices",
    .method = HTTP_POST,
    .handler = devices_post_handler,
};

// --- Config API: /api/config ---

static esp_err_t config_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *aps = cJSON_AddArrayToObject(root, "apartamenti");
    for (int ai = 0; ai < g_cfg.n_apartamenti; ai++) {
        cl_apartamento_t *ap = &g_cfg.ap[ai];
        cJSON *ao = cJSON_CreateObject();
        cJSON_AddStringToObject(ao, "id", ap->id);
        cJSON_AddStringToObject(ao, "label", ap->label);
        cJSON_AddBoolToObject(ao, "attivo", ap->attivo);
        cJSON_AddNumberToObject(ao, "soglia_pesi", ap->soglia_pesi);
        cJSON *grs = cJSON_AddArrayToObject(ao, "gruppi");
        for (int gi = 0; gi < ap->n_gruppi; gi++) {
            cl_gruppo_t *gr = &ap->gruppi[gi];
            cJSON *go = cJSON_CreateObject();
            cJSON_AddStringToObject(go, "id", gr->id);
            cJSON_AddStringToObject(go, "label", gr->label);
            cJSON_AddNumberToObject(go, "delta_acc_comfort", gr->delta_acc_comfort);
            cJSON_AddNumberToObject(go, "delta_acc_eco", gr->delta_acc_eco);
            cJSON_AddNumberToObject(go, "delta_sp_comfort", gr->delta_sp_comfort);
            cJSON_AddNumberToObject(go, "delta_sp_eco", gr->delta_sp_eco);
            // Calendario: serializzazione manuale come raw JSON
            char *calbuf = calloc(1, 7 * (24 * 3 + 4) + 4);
            if (calbuf) {
                char *p = calbuf;
                *p++ = '[';
                for (int gg = 0; gg < CL_GG; gg++) {
                    *p++ = '[';
                    for (int hh = 0; hh < CL_ORA; hh++) {
                        int v = gr->calendario[gg][hh];
                        *p++ = '0' + v;
                        *p++ = ',';
                    }
                    *(p - 1) = ']'; // replace trailing comma
                    *p++ = ',';
                }
                *(p - 1) = ']'; // replace trailing comma
                *p = '\0';
                cJSON_AddRawToObject(go, "calendario", calbuf);
                free(calbuf);
            } else {
                cJSON_AddArrayToObject(go, "calendario");
            }
            cJSON *stz = cJSON_AddArrayToObject(go, "stanze");
            for (int si = 0; si < gr->n_stanze; si++) {
                cJSON_AddItemToArray(stz, cJSON_CreateString(g_cfg.stanze[gr->stanze[si]].id));
            }
            cJSON_AddItemToArray(grs, go);
        }
        cJSON_AddItemToArray(aps, ao);
    }
    // Stanze separate
    cJSON *sts = cJSON_AddArrayToObject(root, "stanze");
    for (int si = 0; si < g_cfg.n_stanze; si++) {
        cl_stanza_t *st = &g_cfg.stanze[si];
        cJSON *so = cJSON_CreateObject();
        cJSON_AddStringToObject(so, "id", st->id);
        cJSON_AddNumberToObject(so, "gruppo", st->gruppo);
        cJSON_AddBoolToObject(so, "inclusa", st->inclusa);
        cJSON_AddNumberToObject(so, "peso", st->peso);
        // Nome leggibile del dispositivo (l'id resta come riferimento).
        const char *nome = "";
        for (int d = 0; d < DEVICES_N; d++) {
            const device_t *dev = devices_get(d);
            if (dev && strcmp(dev->id, st->id) == 0) { nome = dev->nome; break; }
        }
        cJSON_AddStringToObject(so, "nome", nome);
        cJSON_AddItemToArray(sts, so);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Helper: cerca indice stanza per id, crea se non esiste.
static int stanza_ensure(const char *id)
{
    for (int i = 0; i < g_cfg.n_stanze; i++) {
        if (strcmp(g_cfg.stanze[i].id, id) == 0) return i;
    }
    if (g_cfg.n_stanze >= CL_STZ_MAX * CL_GRP_MAX * CL_AP_MAX) return -1;
    cl_stanza_t *st = &g_cfg.stanze[g_cfg.n_stanze];
    memset(st, 0, sizeof(*st));
    snprintf(st->id, sizeof(st->id), "%s", id);
    st->gruppo = -1;
    st->inclusa = true;
    st->peso = 1.0f;
    return g_cfg.n_stanze++;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "payload vuoto o troppo grande");
        return ESP_FAIL;
    }
    char *buf = calloc(1, total + 1);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int rec = httpd_req_recv(req, buf, total);
    if (rec != total) { free(buf); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "short read"); return ESP_FAIL; }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json invalido"); return ESP_FAIL; }

    // Reset config
    memset(&g_cfg, 0, sizeof(g_cfg));

    cJSON *aps = cJSON_GetObjectItem(root, "apartamenti");
    if (cJSON_IsArray(aps)) {
        int ai = 0;
        cJSON *ap_item;
        cJSON_ArrayForEach(ap_item, aps) {
            if (ai >= CL_AP_MAX) break;
            cl_apartamento_t *ap = &g_cfg.ap[ai];
            memset(ap, 0, sizeof(*ap));
            cJSON *tmp;
            tmp = cJSON_GetObjectItem(ap_item, "id");
            if (tmp) snprintf(ap->id, sizeof(ap->id), "%s", tmp->valuestring);
            tmp = cJSON_GetObjectItem(ap_item, "label");
            if (tmp) snprintf(ap->label, sizeof(ap->label), "%s", tmp->valuestring);
            tmp = cJSON_GetObjectItem(ap_item, "attivo");
            ap->attivo = tmp ? cJSON_IsTrue(tmp) : true;
            tmp = cJSON_GetObjectItem(ap_item, "soglia_pesi");
            ap->soglia_pesi = tmp ? (float)tmp->valuedouble : 2.0f;

            cJSON *grs = cJSON_GetObjectItem(ap_item, "gruppi");
            if (cJSON_IsArray(grs)) {
                int gi = 0;
                cJSON *gr_item;
                cJSON_ArrayForEach(gr_item, grs) {
                    if (gi >= CL_GRP_MAX) break;
                    cl_gruppo_t *gr = &ap->gruppi[gi];
                    memset(gr, 0, sizeof(*gr));
                    tmp = cJSON_GetObjectItem(gr_item, "id");
                    if (tmp) snprintf(gr->id, sizeof(gr->id), "%s", tmp->valuestring);
                    tmp = cJSON_GetObjectItem(gr_item, "label");
                    if (tmp) snprintf(gr->label, sizeof(gr->label), "%s", tmp->valuestring);
                    tmp = cJSON_GetObjectItem(gr_item, "delta_acc_comfort");
                    gr->delta_acc_comfort = tmp ? (float)tmp->valuedouble : 0.0f;
                    tmp = cJSON_GetObjectItem(gr_item, "delta_acc_eco");
                    gr->delta_acc_eco = tmp ? (float)tmp->valuedouble : 0.0f;
                    tmp = cJSON_GetObjectItem(gr_item, "delta_sp_comfort");
                    gr->delta_sp_comfort = tmp ? (float)tmp->valuedouble : 0.0f;
                    tmp = cJSON_GetObjectItem(gr_item, "delta_sp_eco");
                    gr->delta_sp_eco = tmp ? (float)tmp->valuedouble : 0.0f;

                    // Calendario
                    cJSON *cal = cJSON_GetObjectItem(gr_item, "calendario");
                    if (cJSON_IsArray(cal)) {
                        int gg = 0;
                        cJSON *day;
                        cJSON_ArrayForEach(day, cal) {
                            if (gg >= CL_GG) break;
                            if (cJSON_IsArray(day)) {
                                int hh = 0;
                                cJSON *val;
                                cJSON_ArrayForEach(val, day) {
                                    if (hh >= CL_ORA) break;
                                    gr->calendario[gg][hh] = (uint8_t)val->valueint;
                                    hh++;
                                }
                            }
                            gg++;
                        }
                    }

                    // Stanze nel gruppo
                    cJSON *stz = cJSON_GetObjectItem(gr_item, "stanze");
                    if (cJSON_IsArray(stz)) {
                        int si = 0;
                        cJSON *sid;
                        cJSON_ArrayForEach(sid, stz) {
                            if (si >= CL_STZ_MAX) break;
                            int idx = stanza_ensure(sid->valuestring);
                            if (idx >= 0) {
                                g_cfg.stanze[idx].gruppo = gi;
                                gr->stanze[si] = idx;
                                si++;
                            }
                        }
                        gr->n_stanze = si;
                    }
                    gi++;
                }
                ap->n_gruppi = gi;
            }
            ai++;
        }
        g_cfg.n_apartamenti = ai;
    }

    // Stanze singole (override di pesi/inclusione)
    cJSON *sts = cJSON_GetObjectItem(root, "stanze");
    if (cJSON_IsArray(sts)) {
        cJSON *st_item;
        cJSON_ArrayForEach(st_item, sts) {
            cJSON *tmp = cJSON_GetObjectItem(st_item, "id");
            if (!tmp) continue;
            int idx = stanza_ensure(tmp->valuestring);
            if (idx < 0) continue;
            tmp = cJSON_GetObjectItem(st_item, "inclusa");
            if (tmp) g_cfg.stanze[idx].inclusa = cJSON_IsTrue(tmp);
            tmp = cJSON_GetObjectItem(st_item, "peso");
            if (tmp) g_cfg.stanze[idx].peso = (float)tmp->valuedouble;
        }
    }

    cJSON_Delete(root);
    cl_config_save();
    ESP_LOGI(TAG, "config aggiornata: %d ap, %d stanze", g_cfg.n_apartamenti, g_cfg.n_stanze);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t config_reset_handler(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
        return ESP_FAIL;
    }
    memset(&g_cfg, 0, sizeof(g_cfg));
    nvs_handle_t h;
    if (nvs_open("climaoro", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "config resettata a default");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"reset effettuato, riavviare\"}");
    return ESP_OK;
}

static const httpd_uri_t uri_config_get = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = config_get_handler,
};

static const httpd_uri_t uri_config_post = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = config_post_handler,
};

static const httpd_uri_t uri_config_reset = {
    .uri = "/api/config/reset",
    .method = HTTP_POST,
    .handler = config_reset_handler,
};

// --- Master API: /api/master ---

static esp_err_t master_handler(httpd_req_t *req)
{
    if (req->method != HTTP_GET && req->method != HTTP_POST) {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
        return ESP_FAIL;
    }
    if (req->method == HTTP_POST) {
        int total = req->content_len;
        if (total <= 0 || total > 256) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "payload invalido");
            return ESP_FAIL;
        }
        char buf[256];
        int rec = httpd_req_recv(req, buf, total);
        if (rec != total) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "short read"); return ESP_FAIL; }
        buf[total] = '\0';
        cJSON *root = cJSON_Parse(buf);
        if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json invalido"); return ESP_FAIL; }
        cJSON *on = cJSON_GetObjectItem(root, "master");
        if (on) engine_set_master(cJSON_IsTrue(on));
        cJSON_Delete(root);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "master", engine_master());
    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_master_get = {
    .uri = "/api/master",
    .method = HTTP_GET,
    .handler = master_handler,
};

static const httpd_uri_t uri_master_post = {
    .uri = "/api/master",
    .method = HTTP_POST,
    .handler = master_handler,
};

// --- ClimaOro status: /api/climaoro/status ---

static esp_err_t climaoro_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "master", engine_master());

    cJSON *aps = cJSON_AddArrayToObject(root, "appartamenti");
    for (int ai = 0; ai < g_cfg.n_apartamenti; ai++) {
        cl_apartamento_t *ap = &g_cfg.ap[ai];
        cJSON *ao = cJSON_CreateObject();
        cJSON_AddStringToObject(ao, "id", ap->id);
        cJSON_AddStringToObject(ao, "label", ap->label);
        cJSON_AddBoolToObject(ao, "attivo", ap->attivo);
        cJSON_AddNumberToObject(ao, "soglia_pesi", ap->soglia_pesi);

        cJSON *grs = cJSON_AddArrayToObject(ao, "gruppi");
        for (int gi = 0; gi < ap->n_gruppi; gi++) {
            cl_gruppo_t *gr = &ap->gruppi[gi];
            cJSON *go = cJSON_CreateObject();
            cJSON_AddStringToObject(go, "id", gr->id);
            cJSON_AddStringToObject(go, "label", gr->label);

            // Modalita' corrente dal calendario
            int fascia = fascia_ora(0, gr->calendario);
            const char *fascia_str = (fascia == CL_COMFORT) ? "comfort" :
                                     (fascia == CL_ECO) ? "eco" : "autonomo";
            cJSON_AddStringToObject(go, "modalita", fascia_str);

            // Stanze del gruppo
            cJSON *sts = cJSON_AddArrayToObject(go, "stanze");
            float totale_pesi = 0.0f;
            int n_richiedenti = 0;
            for (int si = 0; si < gr->n_stanze; si++) {
                int sii = gr->stanze[si];
                cl_stanza_t *st = &g_cfg.stanze[sii];
                cJSON *so = cJSON_CreateObject();
                cJSON_AddStringToObject(so, "id", st->id);
                cJSON_AddNumberToObject(so, "peso", st->peso);
                cJSON_AddBoolToObject(so, "inclusa", st->inclusa);

                // Cerca il device corrispondente
                int di = -1;
                for (int d = 0; d < DEVICES_N; d++) {
                    const device_t *dev = devices_get(d);
                    if (dev && strcmp(dev->id, st->id) == 0) { di = d; break; }
                }
                // Espone anche il nome leggibile del dispositivo (l'id resta).
                if (di >= 0) {
                    const device_t *dev = devices_get(di);
                    if (dev) cJSON_AddStringToObject(so, "nome", dev->nome);
                } else {
                    cJSON_AddStringToObject(so, "nome", "");
                }
                if (di >= 0 && g_state[di].online) {
                    double t = esph_value(di, "sensor-temperatura_reale", -273.15);
                    double u = esph_value(di, "sensor-umidit___reale", -1.0);
                    double ts = esph_value(di, "number-temperatura_salvata", 0.0);
                    if (t > -100.0) cJSON_AddNumberToObject(so, "temp_reale", t);
                    if (u >= 0.0) cJSON_AddNumberToObject(so, "umidita", u);
                    if (ts > 0.0) cJSON_AddNumberToObject(so, "temp_salvata", ts);
                    cJSON_AddBoolToObject(so, "online", true);

                    const ent_t *cl = esph_find(di, "climate-climatizzazione");
                    if (cl) {
                        if (cl->action[0]) cJSON_AddStringToObject(so, "action", cl->action);
                    }

                    // Conta richiedenti (temp_reale <= temp_salvata - 0.5)
                    if (st->inclusa && t > -100.0 && ts > 0.0 && t <= ts - 0.5) {
                        totale_pesi += st->peso;
                        n_richiedenti++;
                    }
                } else {
                    cJSON_AddBoolToObject(so, "online", false);
                }
                cJSON_AddItemToArray(sts, so);
            }
            cJSON_AddNumberToObject(go, "totale_pesi", totale_pesi);
            cJSON_AddNumberToObject(go, "richiedenti", n_richiedenti);
            cJSON_AddBoolToObject(go, "soglia_raggiunta", totale_pesi >= ap->soglia_pesi);
            cJSON_AddItemToArray(grs, go);
        }
        cJSON_AddItemToArray(aps, ao);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_climaoro_status = {
    .uri = "/api/climaoro/status",
    .method = HTTP_GET,
    .handler = climaoro_status_handler,
};

static void server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;   // default 4096: troppo poco se un handler
                                // dichiara array grandi sullo stack
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &uri_status);
        httpd_register_uri_handler(s_server, &uri_devices);
        httpd_register_uri_handler(s_server, &uri_devices_post);
        httpd_register_uri_handler(s_server, &uri_config_get);
        httpd_register_uri_handler(s_server, &uri_config_post);
        httpd_register_uri_handler(s_server, &uri_config_reset);
        httpd_register_uri_handler(s_server, &uri_master_get);
        httpd_register_uri_handler(s_server, &uri_master_post);
        httpd_register_uri_handler(s_server, &uri_climaoro_status);
        ESP_LOGI(TAG, "server http attivo su porta %d", config.server_port);
    }
}

void app_main(void)
{
    led_init();
    esph_init();

    // Redirect cJSON in PSRAM: evita frammentazione della SRAM interna
    // durante settimane di polling continuo. Se la PSRAM non e' presente
    // o non inizializzata, cjson_psram_malloc ripiega sulla SRAM.
#if defined(CONFIG_SPIRAM)
    cJSON_Hooks json_hooks = {
        .malloc_fn = cjson_psram_malloc,
        .free_fn   = cjson_psram_free,
    };
    cJSON_InitHooks(&json_hooks);
    ESP_LOGI(TAG, "cJSON redirect su PSRAM");
#else
    ESP_LOGI(TAG, "cJSON su SRAM (nessuna PSRAM dichiarata)");
#endif

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "climaoro-motore avvio...");
    wifi_init();

    if (xEventGroupGetBits(s_event_group) & WIFI_CONNECTED_BIT) {
        cl_config_init();
        devices_init();
        server_start();
        engine_start();
    }
}
