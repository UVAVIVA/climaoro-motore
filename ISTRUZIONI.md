# Installazione CLIMAORO Motore

## Installazione Automatica (consigliata)

### Windows

1. Scarica il file [`install.bat`](https://github.com/UVAVIVA/climaoro-motore/raw/main/install.bat)&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;[(DOWNLOAD QUI)](https://github.com/UVAVIVA/climaoro-motore/raw/main/install.bat)
2. Fai **doppio clic** su `install.bat`
3. Lo script:
   - Installa Python e PlatformIO se non ci sono gia
   - Scarica il codice del motore da GitHub
   - Ti chiede i tuoi dati (WiFi, IP, termostati)
   - Compila e carica il firmware sull'ESP32
   - Avvia il monitor seriale per vedere i log

### Linux / macOS

1. Scarica il file [`install.sh`](install.sh)
2. Apri un terminale nella cartella del file
3. Rendilo eseguibile e avvialo:
   ```bash
   chmod +x install.sh
   ./install.sh
   ```
4. Rispondi alle domande dello script

---

## Installazione Manuale

Se preferisci fare tutto a mano:

### Prerequisiti

1. **Python 3**: Scaricalo da [python.org](https://www.python.org/downloads/)
2. **PlatformIO**: Apri un terminale e scrivi:
   ```bash
   pip install platformio
   ```

### Passi

1. Scarica il codice dal repository
2. Copia `src/secrets.h.example` in `src/secrets.h` e compilalo con i tuoi dati WiFi e IP
3. Copia `src/devices_example.c` in `src/devices.c` e compilalo con i tuoi termostati
4. Apri il terminale nella cartella del progetto
5. Compila e carica:
   ```bash
   pio run
   pio run -t upload
   ```

---

## Risoluzione Problemi

### L'ESP32 non viene rilevato
- Usa un **cavo USB dati** (non solo ricarica)
- Prova un'altra porta USB
- Installa i driver CH343 per la tua scheda

### La compilazione fallisce
- Controlla che `src/secrets.h` esista e sia compilato correttamente
- Controlla che `src/devices.c` esista e sia compilato correttamente
- Leggi i messaggi di errore nel terminale

### Lo script si blocca
- Riavvialo: i tuoi dati gia inseriti non vengono persi
- Controlla la connessione internet

### Il firmware non parte
- Avvia il monitor seriale per vedere gli errori:
  ```bash
  pio device monitor
  ```
- Verifica che l'IP del motore sia nella stessa rete del router
