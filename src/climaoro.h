#pragma once

#include <stdbool.h>
#include <stdint.h>

// Giorni della settimana: 0=lun ... 6=dom
#define CL_GG 7
#define CL_ORA 24

// Valori possibili per ogni ora del calendario
#define CL_COMFORT 0
#define CL_ECO     1
#define CL_AUTONOMO 2

// Fino a 2 appartamenti, 4 gruppi ciascuno, 8 stanze per gruppo
#define CL_AP_MAX  2
#define CL_GRP_MAX 4
#define CL_STZ_MAX 8

typedef struct {
    char    id[40];          // es. UUID del dispositivo (36 char + NUL);
                             // deve combaciare con device_t.id per il match
    int     gruppo;          // indice nel gruppo (-1 = non assegnato)
    bool    inclusa;         // inclusione attiva
    float   peso;            // 0..5
} cl_stanza_t;

typedef struct {
    char    id[16];          // es. "zona_giorno"
    char    label[32];
    // Calendario: 7 giorni x 24 ore. Ogni byte = CL_COMFORT/CL_ECO/CL_AUTONOMO
    uint8_t calendario[CL_GG][CL_ORA];
    float   delta_acc_comfort;
    float   delta_acc_eco;
    float   delta_sp_comfort;
    float   delta_sp_eco;
    int     n_stanze;
    int     stanze[CL_STZ_MAX];  // indice in stanzе[]
} cl_gruppo_t;

typedef struct {
    char    id[16];
    char    label[32];
    bool    attivo;
    float   soglia_pesi;
    int     n_gruppi;
    cl_gruppo_t gruppi[CL_GRP_MAX];
} cl_apartamento_t;

typedef struct {
    int     n_apartamenti;
    cl_apartamento_t ap[CL_AP_MAX];
    int     n_stanze;
    cl_stanza_t stanze[CL_STZ_MAX * CL_GRP_MAX * CL_AP_MAX];
} cl_config_t;

// Configurazione globale (caricata da NVS, modificabile via API REST).
extern cl_config_t g_cfg;

// Carica da NVS, se vuoto inizializza con default.
void cl_config_init(void);

// Salva in NVS.
void cl_config_save(void);

// Logica decisionale: chiama dopo esph_poll di tutti i dispositivi.
// Legge gli stati da g_state[], comanda i termostati via esph_*.
void cl_decidi(void);

// Fascia oraria corrente: restituisce CL_COMFORT, CL_ECO o CL_AUTONOMO.
int fascia_ora(int ora, const uint8_t calendario[CL_GG][CL_ORA]);
