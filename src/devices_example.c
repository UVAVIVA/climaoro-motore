#include "devices.h"
#include <stddef.h>

// ============================================================
// Esempio: copia questo file in "devices.c" e adatta alla
// tua installazione. Ogni riga descrive un termostato.
//
// Campi:
//   id      - identificativo breve (deve corrispondere all'id
//             del termostato ESPHome, es. "b2")
//   nome    - nome leggibile (es. "SOGGIORNO")
//   ip      - indirizzo IP assegnato al termostato
//   tipo    - DEV_THERMOSTAT (termostato autonomo)
//   attivo  - true = incluso nel ciclo motore
//
// DEV_MAX = 16 (massimo dispositivi supportati).
// ============================================================

const device_t DEVICES[DEV_MAX] = {
    // id          nome            ip                tipo             attivo
    { "dev1",      "STANZA_1",     "192.168.1.201",  DEV_THERMOSTAT,  true },
    { "dev2",      "STANZA_2",     "192.168.1.202",  DEV_THERMOSTAT,  true },
    // { "dev3",   "STANZA_3",     "192.168.1.203",  DEV_THERMOSTAT,  true },
    // { "dev4",   "STANZA_4",     "192.168.1.204",  DEV_THERMOSTAT,  true },
};

int DEVICES_N = 2;

const device_t *devices_get(int i)
{
    if (i < 0 || i >= DEVICES_N) return NULL;
    return &DEVICES[i];
}
