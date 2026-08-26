# CLIMAORO Motore

Motore decisionale autonomo per il sistema CLIMAORO, basato su **ESP32-S3** con ESP-IDF nativo.

## Storia

CLIMAORO nasce come sistema di climatizzazione centralizzata per apartamenti multiproprieta'. Inizialmente il "motore" girava su **AppDaemon** all'interno di Home Assistant: leggeva gli stati dei termostati ESPHome, applicava la logica decisionale e inviava comandi.

Questo firmware riproduce la stessa identica logica su un **ESP32-S3 dedicato**, eliminando la dipendenza da Home Assistant e AppDaemon. Il telefono (app Flutter) resta solo l'interfaccia utente.

## Architettura

```
Termostati ESPHome (6x)  ──ESP-NOW──>  Collettori valvole
       │                                       │
       └──── HTTP (web_server) ────────────────┘
                     │
              ESP32-S3 Motore
              (questo firmware)
                     │
              HTTP REST API
                     │
              App Flutter (telefono)
```

Il motore:
1. **Polling** - Ogni 60 secondi interroga tutti i termostati via HTTP (`/events`)
2. **Decisione** - Applica la logica ClimaOro: legge calendario, temperatura, umidita', e decide cosa fare
3. **Comandi** - Invia comandi ai termostati via HTTP (stessa interfaccia dell'app)
4. **Manutenzione** - Gestisce la modalita' centralizzata (rinnovo ogni 240s, ri-assert ogni 1200s)

## Logica Decisionale

Il motore implementa la logica completa ClimaOro:

- **Calendario** - 7 giorni x 24 ore, ogni ora puo' essere COMFORT, ECO o AUTONOMO
- **Gruppi** - Le stanze sono raggruppate; ciascun gruppo ha i propri delta di accensione/spegnimento
- **Pesi** - Ogni stanza ha un peso; quando il totale pesi supera la soglia, il gruppo si accende
- **Priorita'** - Se una stanza sta gia' scaldando, ha priorita' e non viene toccata
- **Centralizzata** - Il motore assicura che la modalita' centralizzata sia sempre attiva prima di ogni comando

## Configurazione

### 1. Credenziali WiFi e IP

Copia `src/secrets.h.example` in `src/secrets.h` e compila:

```c
#define WIFI_SSID1 "LaTuaRete"
#define WIFI_PASS1 "LaPassword"
#define MOTORE_IP      "192.168.1.196"
#define MOTORE_GATEWAY "192.168.1.1"
#define MOTORE_NETMASK "255.255.255.0"
```

**Attenzione**: `secrets.h` e' nel `.gitignore` e non viene commesso su Git.

### 2. Elenco dispositivi

Modifica `src/devices.c` con i tuoi termostati:

```c
const device_t DEVICES[DEV_MAX] = {
    { "soggiorno", "SOGGIORNO", "192.168.1.201", DEV_THERMOSTAT, true },
    { "salotto",   "SALOTTO",   "192.168.1.202", DEV_THERMOSTAT, true },
};
int DEVICES_N = 2;
```

Campi:
- `id` - Identificativo breve (deve corrispondere all'id ESPHome del termostato)
- `nome` - Nome leggibile
- `ip` - Indirizzo IP del termostato
- `tipo` - `DEV_THERMOSTAT` (termostato) o `DEV_COLLECTOR` (collettore valvole)
- `attivo` - `true` se incluso nel ciclo motore

`DEV_MAX = 16` (massimo dispositivi supportati).

### 3. Slug entita'

Modifica `src/slugs.h` se i nomi dei componenti nei tuoi YAML ESPHome sono diversi. Gli slug derivano dal campo `NAME` nei file YAML, con gli accenti sostituiti da underscore.

### 4. Configurazione runtime

La configurazione (appartamenti, gruppi, stanze, calendario) si gestisce via API REST o dall'app Flutter. Il motore salva tutto in NVS (flash interna).

## Build

### Prerequisiti

- [PlatformIO](https://platformio.org/) (VS Code extension o CLI)
- ESP-IDF (installato automaticamente da PlatformIO)

### Comando

```bash
pio run
```

L'output sara' in `.pio/build/climaoro-motore/firmware.bin`.

### Flash

```bash
esptool.py --port COMx --baud 460800 write_flash 0x10000 .pio/build/climaoro-motore/firmware.bin
```

Sostituisci `COMx` con la porta seriale corretta.

## API REST

| Endpoint | Metodo | Descrizione |
|----------|--------|-------------|
| `/api/status` | GET | Stato generale del motore |
| `/api/devices` | GET | Lista dispositivi configurati |
| `/api/config` | GET/POST | Configurazione appartamenti/gruppi/stanze |
| `/api/config/reset` | POST | Reset config a default |
| `/api/master` | GET/POST | Stato/attivazione master |
| `/api/climaoro/status` | GET | Stato dettagliato decision engine |

## Hardware

- **Scheda**: ESP32-S3 (QFN56, rev v0.2)
- **Flash**: 16MB XMC
- **PSRAM**: 8MB OCT AP_3v3
- **USB-UART**: CH343 (UART0, 115200 baud)
- **Partizioni**: 16MB layout (app0/app1 = 3MB, coredump = 192KB)

## File principali

| File | Descrizione |
|------|-------------|
| `src/main.c` | WiFi, HTTP server, tutti gli endpoint |
| `src/engine.c` | Ciclo motore 60s (poll, centralizzata, master) |
| `src/climaoro.c` | Logica decisionale (calendario, pesi, comandi) |
| `src/esph.c` | Comunicazione HTTP con termostati ESPHome |
| `src/devices.c` | Elenco dispositivi (configurare qui) |
| `src/slugs.h` | Slug entita' web (configurare qui) |
| `src/secrets.h` | Credenziali WiFi/IP (NON commettere) |

## Licenza

MIT con Condizione di Attribuzione - vedi `LICENSE` e `ATTRIBUZIONE.md`.

## Autore

UVAVIVA - https://github.com/UVAVIVA
