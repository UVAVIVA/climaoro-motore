#pragma once

// ============================================================
// Slug web delle entita' sui termostati ESPHome.
//
// Derivano dal campo NAME nei file YAML dei termostati.
// Gli accenti vengono sostituiti con underscore, gli spazi
// eliminati.
//
// Esempio YAML:
//   - platform: ...
//     name: "Modalita' Centralizzata"
//   Slug risultante: "modalit___centralizzata"
//
// Modifica questi valori se i nomi nei tuoi YAML diversi.
// ============================================================

// Termostato
#define SLUG_CLIMATE         "climatizzazione"
#define SLUG_TEMP_SAL        "temperatura_salvata"
#define SLUG_TEMP_SAL_C      "temperatura_salvata_cool"

// Modalita' centralizzata
#define SLUG_SWITCH          "modalit___centralizzata"
#define SLUG_RINNOVA         "rinnova_modalit___centralizzata"

// Sensori (per il log)
#define SLUG_SENSOR_TEMP     "sensor-temperatura_reale"
#define SLUG_SENSOR_UMI      "sensor-umidit___reale"
#define SLUG_SENSOR_TSAL     "number-temperatura_salvata"
