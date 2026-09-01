#pragma once

#include <stdbool.h>

// Tipo di dispositivo gestito dal motore.
typedef enum {
    DEV_THERMOSTAT,   // termostato autonomo (b1..e1)
    DEV_COLLECTOR,    // collettore valvole
} dev_type_t;

// Descrizione di un dispositivo.
// Nessun dispositivo e' hardcoded: la lista viene ricevuta dall'app
// (POST /api/devices) e salvata in NVS.
typedef struct {
    char     id[40];    // id stanza usato dalla config: e' l'UUID del
                        // dispositivo dato dall'app (36 char + NUL); va
                        // tenuto intero per combaciare con cl_stanza_t.id
    char     nome[32];  // nome leggibile
    char     ip[16];    // indirizzo IP del termostato
    dev_type_t tipo;
    bool     attivo;    // incluso nel ciclo poll/comandi
} device_t;

#define DEV_MAX 16

extern int DEVICES_N;

const device_t *devices_get(int i);

// Carica la lista da NVS (all'avvio). Se assente: 0 dispositivi.
void devices_init(void);

// Sostituisce la lista (arriva dall'app) e la salva in NVS.
// Azzera anche lo stato ESPHome associato. Ritorna il numero caricato.
int devices_set(const device_t *list, int n);
