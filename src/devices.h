#pragma once

#include <stdbool.h>

// Tipo di dispositivo gestito dal motore.
typedef enum {
    DEV_THERMOSTAT,   // termostato autonomo (b1..e1)
    DEV_COLLECTOR,    // collettore valvole
} dev_type_t;

// Descrizione statica di un dispositivo.
// Per aggiungerne uno: aggiungere una riga in DEVICES (devices.c).
typedef struct {
    const char *id;     // breve, es. "b2"
    const char *nome;   // descrittivo, es. "SOGGIORNO"
    const char *ip;
    dev_type_t tipo;
    bool attivo;        // incluso nel ciclo poll/comandi
} device_t;

#define DEV_MAX 16

extern const device_t DEVICES[DEV_MAX];
extern int DEVICES_N;

const device_t *devices_get(int i);
