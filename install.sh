#!/bin/bash

echo "============================================"
echo " CLIMAORO Motore - Installazione guidata"
echo "============================================"
echo ""
read -p "Premi Invio per continuare..."

# --- 1. Controlla PlatformIO ---
echo ""
echo "[1/5] Controllo PlatformIO..."
if ! command -v pio &> /dev/null; then
    echo "PlatformIO non trovato."
    read -p "  Vuoi installare PlatformIO adesso? (s/n): " INSTALL_PIO
    if [ "$INSTALL_PIO" = "s" ]; then
        if ! command -v python3 &> /dev/null; then
            echo "[ERRORE] Python3 non trovato. Installalo prima:"
            echo "  Ubuntu/Debian: sudo apt install python3 python3-pip"
            echo "  macOS: brew install python3"
            exit 1
        fi
        pip3 install platformio
        if [ $? -ne 0 ]; then
            echo "[ERRORE] Installazione PlatformIO fallita."
            exit 1
        fi
        echo "[OK] PlatformIO installato."
    else
        echo "PlatformIO e necessario per compilare."
        exit 1
    fi
else
    echo "[OK] PlatformIO trovato."
fi
echo ""
read -p "Premi Invio per continuare..."

# --- 2. Scarica il codice ---
echo ""
echo "[2/5] Download codice..."
if [ ! -d "climaoro-motore" ]; then
    echo "Download da GitHub..."
    curl -L --fail -o climaoro-motore.zip https://github.com/UVAVIVA/climaoro-motore/archive/refs/heads/main.zip
    if [ $? -ne 0 ]; then
        echo ""
        echo "[ERRORE] Download fallito."
        echo "Possibili cause:"
        echo "  - Nessuna connessione internet"
        echo "  - Il repository non e ancora pubblico"
        echo "  - URL non valido"
        exit 1
    fi
    unzip -q climaoro-motore.zip
    mv climaoro-motore-main climaoro-motore
    rm climaoro-motore.zip
    echo "[OK] Codice scaricato."
else
    echo "[OK] Codice gia presente."
fi
echo ""

# --- 3. Entra nella cartella ---
if [ ! -d "climaoro-motore" ]; then
    echo "[ERRORE] Cartella non trovata dopo il download."
    exit 1
fi
cd climaoro-motore

# --- 4. Chiedi dati WiFi ---
echo ""
echo "[3/5] Configurazione WiFi e Rete."
echo ""
read -p "  SSID (nome rete): " WIFI_SSID
read -p "  Password WiFi: " WIFI_PASS
read -p "  IP del motore (es. 192.168.1.196): " MOTORE_IP
read -p "  Gateway (es. 192.168.1.1): " MOTORE_GW
read -p "  Netmask (es. 255.255.255.0): " MOTORE_NM

cat > src/secrets.h <<EOL
#pragma once

#define WIFI_SSID1 "$WIFI_SSID"
#define WIFI_PASS1 "$WIFI_PASS"
#define WIFI_SSID2 ""
#define WIFI_PASS2 ""

#define MOTORE_IP      "$MOTORE_IP"
#define MOTORE_GATEWAY "$MOTORE_GW"
#define MOTORE_NETMASK "$MOTORE_NM"
EOL
echo "[OK] Configurazione WiFi salvata."
echo ""

# --- 5. Chiedi termostati ---
echo "[4/5] Configurazione Termostati."
echo ""
read -p "  Quanti termostati? " NUM_DEV

cat > src/devices.c << 'EOFHEADER'
#include "devices.h"
#include <stddef.h>

const device_t DEVICES[DEV_MAX] = {
EOFHEADER

for ((i=1; i<=NUM_DEV; i++)); do
    echo ""
    echo "  --- Termostato $i ---"
    read -p "    ID (es. b2): " DEV_ID
    read -p "    Nome (es. SOGGIORNO): " DEV_NAME
    read -p "    IP (es. 192.168.1.212): " DEV_IP
    read -p "    Tipo (0=termostato, 1=collettore): " DEV_TYPE
    read -p "    Attivo (1=si, 0=no): " DEV_ACTIVE

    if [ "$DEV_ACTIVE" = "1" ]; then
        DEV_ACTIVE_BOOL="true"
    else
        DEV_ACTIVE_BOOL="false"
    fi

    if [ "$DEV_TYPE" = "1" ]; then
        DEV_TYPE_ENUM="DEV_COLLECTOR"
    else
        DEV_TYPE_ENUM="DEV_THERMOSTAT"
    fi

    echo "    { \"$DEV_ID\", \"$DEV_NAME\", \"$DEV_IP\", $DEV_TYPE_ENUM, $DEV_ACTIVE_BOOL }," >> src/devices.c
done

cat >> src/devices.c << EOFCONTENT
};

int DEVICES_N = $NUM_DEV;

const device_t *devices_get(int i)
{
    if (i < 0 || i >= DEVICES_N) return NULL;
    return &DEVICES[i];
}
EOFCONTENT
echo ""
echo "[OK] Configurazione termostati salvata."
echo ""
read -p "Premi Invio per continuare..."

# --- 6. Compilazione ---
echo ""
echo "[5/5] Compilazione in corso..."
echo ""
pio run
if [ $? -ne 0 ]; then
    echo ""
    echo "[ERRORE] Compilazione fallita."
    exit 1
fi
echo ""
echo "[OK] Compilazione completata."
echo ""

# --- 7. Upload ---
echo "Caricamento firmware sull'ESP32..."
echo ""
pio run -t upload
if [ $? -ne 0 ]; then
    echo ""
    echo "[ERRORE] Caricamento fallito."
    exit 1
fi
echo ""
echo "============================================"
echo " Installazione completata con successo!"
echo "============================================"
echo ""

# --- 8. Monitor seriale ---
echo "Avvio monitor seriale..."
echo "(premi Ctrl+C per uscire)"
echo ""
pio device monitor
