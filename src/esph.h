#pragma once

#include <stdbool.h>
#include <stdint.h>

// Entita' di un dispositivo come vista dal web server ESPHome.
typedef struct {
    char   id[48];      // es. "sensor-temperatura_reale"
    char   name[48];    // es. "Temperatura Reale"
    char   state[32];
    double value;
    bool   has_value;
    // Solo per l'entita' clima:
    char   mode[8];     // "OFF"/"HEAT"/"COOL"
    char   action[8];   // "OFF"/"IDLE"/"HEATING"/"COOLING"
} ent_t;

// Stato corrente di un dispositivo (riempito dal poll).
typedef struct {
    bool     online;
    int64_t  last_ok_ms;   // ultimo poll riuscito (ms da avvio)
    int      n;
    ent_t    e[64];
} dev_state_t;

// Tabelle di stato indicizzate come DEVICES (devices.h).
// Allocata dinamicamente in PSRAM da esph_init().
extern dev_state_t *g_state;

// Alloca g_state[] in PSRAM. Da chiamare PRIMA di tutto in app_main.
void esph_init(void);

// Trova un'entita' per id esatto (es. "climate-climatizzazione").
const ent_t *esph_find(int di, const char *ent_id);
double esph_value(int di, const char *ent_id, double fallback);

// Poll: legge il burst iniziale di /events (tutte le entita').
// Ritorna true se arrivato almeno un'entita'.
bool esph_poll(int di, const char *ip);

// Comandi (dialetto web_server, identico a quello dell'app).
bool esph_climate_set(const char *ip, const char *slug,
                      const char *mode,
                      const double *low, const double *high);
bool esph_number_set(const char *ip, const char *slug, double value);
bool esph_switch_set(const char *ip, const char *slug, bool on);
bool esph_button_press(const char *ip, const char *slug);
