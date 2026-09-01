#pragma once

#include <stdbool.h>

// Stato master (persistito in NVS) e ciclo di engine.
bool engine_master(void);
void engine_set_master(bool on);

// Modalita' desiderata per un dispositivo (ri-assert periodico).
void engine_set_desired(int di, const char *mode);

// Avvia il task di polling/decisione.
void engine_start(void);