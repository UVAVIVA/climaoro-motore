#include "devices.h"
#include <stddef.h>

// Elenco dispositivi gestiti dal motore.
// Configurare qui con i propri termostati.
// Campi: id (breve), nome (leggibile), ip, tipo, attivo.

const device_t DEVICES[DEV_MAX] = {
    // id          nome            ip                tipo             attivo
    { "b2",         "SOGGIORNO",    "192.168.1.212",  DEV_THERMOSTAT,  true },
    { "b3",         "SALOTTO",      "192.168.1.213",  DEV_THERMOSTAT,  true },
    { "b4",         "UFFICIO",      "192.168.1.214",  DEV_THERMOSTAT,  true },
    { "cameretta",  "CAMERETTA",    "192.168.1.204",  DEV_THERMOSTAT,  true },
    { "camera_osp", "CAMERA OSPITI","192.168.1.205",  DEV_THERMOSTAT,  true },
    { "bagno_osp",  "BAGNO OSPITI", "192.168.1.206",  DEV_THERMOSTAT,  true },
};

int DEVICES_N = 6;

const device_t *devices_get(int i)
{
    if (i < 0 || i >= DEVICES_N) return NULL;
    return &DEVICES[i];
}
