# CLIMAORO Motore
**Il cervello stand-alone per la gestione intelligente del riscaldamento a pavimento su ESP32-S3.**

---

## Links

- **Sito web:** [https://UVAVIVA.github.io/CLIMAORO/](https://UVAVIVA.github.io/CLIMAORO/)
- **Progetto principale:** [https://github.com/UVAVIVA/CLIMAORO](https://github.com/UVAVIVA/CLIMAORO)

---

## L'Idea

CLIMAORO nasce per risolvere un problema concreto: coordinare il riscaldamento di un appartamento multiproprietà in modo fluido, affidabile e senza punti unici di fallimento.

Nelle prime versioni, la logica decisionale risiedeva all'interno di Home Assistant tramite uno script AppDaemon. Sebbene funzionante, la soluzione soffriva di un limite strutturale: l'infrastruttura di riscaldamento dipendeva dalla stabilità dell'infrastruttura domotica generale. Se il server si bloccava o andava in manutenzione, l'impianto smetteva di prendere decisioni.

La svolta è stata la **decentralizzazione**: trasferire l'intero motore decisionale su un microcontrollore dedicato ESP32-S3. Un hardware essenziale, economico e privo di dipendenze esterne. Ogni 60 secondi valuta i sensori, consulta il calendario di zona e comanda i termostati. Senza server, senza cloud, senza interruzioni.

---

## Foto

[![Motore 1](images/00b52f18-fcd2-4f22-bc78-fc6dbafb9023.jpg)](images/00b52f18-fcd2-4f22-bc78-fc6dbafb9023.jpg)
[![Motore 2](images/48d5ad94-720a-4e18-af88-9c372f740eee.jpg)](images/48d5ad94-720a-4e18-af88-9c372f740eee.jpg)
[![Motore 3](images/91745b8e-12b7-4d81-99e9-c87701b7c1f8.jpg)](images/91745b8e-12b7-4d81-99e9-c87701b7c1f8.jpg)
[![Motore 4](images/a965ce88-6679-4776-bdce-42e957b538df.jpg)](images/a965ce88-6679-4776-bdce-42e957b538df.jpg)

---

## Come Funziona

L'architettura del sistema si articola su tre livelli ben distinti:

| Componente | Ruolo | Descrizione |
| --- | --- | --- |
| **Termostati** | *Esecutori* | ESP32 periferici posizionati nelle singole stanze. Leggono temperatura e umidità, pilotano gli attuatori del pavimento radiante ed espongono endpoint web in rete locale. |
| **Motore** *(questo firmware)* | *Coordinatore* | Interroga periodicamente i termostati via HTTP/SSE, applica le regole di gruppo e la matrice oraria, inviando i comandi di accensione o spegnimento. |
| **App** | *Interfaccia* | Client multipiattaforma (Flutter) per monitoraggio e configurazione. **Il motore è del tutto autonomo**: se l'app è chiusa o disconnessa, la regolazione prosegue indisturbata. |

---

## Logica Decisionale

Il motore opera sulla base di una matrice settimanale (7 giorni × 24 ore) con tre stati operativi per ogni blocco orario:

- **Comfort**: Riscaldamento attivo puntato al setpoint di regime.
- **Eco**: Modalità a risparmio energetico con setpoint ridotto.
- **Autonomo**: Il motore sospende l'algoritmo centrale e lascia la stanza al controllo locale del termostato.

### Algoritmo di Gruppo e Priorità

Quando una zona lavora in modalità *Comfort* o *Eco*, le stanze vengono organizzate in **gruppi**. Per ciascun gruppo l'algoritmo valuta:

1. **Delta di Ingresso / Uscita**: Margini termici dedicati per attivazione e spegnimento.
2. **Soglia Pesi**: Una somma pesata dei fabbisogni delle stanze per evitare micro-accensioni inefficienti del generatore.
3. **Priorità di Mantenimento**: Se una stanza del gruppo è già in fase di riscaldamento (`heating`), ha la precedenza e mantiene l'impianto attivo.
4. **Verifica della Centralizzata**: Prima di ogni cambio di setpoint, il motore verifica che la modalità centralizzata sia attiva sul termostato destinatario (con retry fino a 2 tentativi).

---

## Specifiche Hardware

| Parametro | Dettaglio |
| --- | --- |
| **SoC** | ESP32-S3 (Dual-Core Xtensa LX7 @ 240MHz) |
| **Memoria** | 16 MB Flash, 8 MB PSRAM (utilizzata per lo stato dei dispositivi senza frammentare la SRAM) |
| **Interfaccia USB/Seriale** | CH343 (Baudrate log: 115200) |
| **Capacità Massima** | Fino a **16 dispositivi/termostati** gestibili in parallelo |
| **Integrazione** | Moduli ESPHome esistenti (nessun firmware custom richiesto sui termostati) |

---

## Installazione

### 1. Configura le credenziali WiFi

Copia [`src/secrets.h.example`](src/secrets.h.example) in `src/secrets.h` e imposta il tuo SSID, password e IP statico del motore.

### 2. Compila il firmware

```bash
pio run
```

Il file binario sarà in `.pio/build/climaoro-motore/firmware.bin`.

### 3. Flasha l'ESP32

```bash
esptool.py --port COMx --baud 460800 write_flash 0x10000 .pio/build/climaoro-motore/firmware.bin
```

*(Sostituisci `COMx` con la porta seriale corretta).*

### 4. Installa l'App e aggiungi i termostati

Dopo il flash, il motore è online. Installa l'App Flutter ([climaoro-app](https://github.com/UVAVIVA/climaoro-app)) e aggiungi i termostati dalla schermata di configurazione.

---

## API REST

Il motore espone un set completo di API HTTP per l'integrazione con l'App e con sistemi terzi:

| Endpoint | Metodo | Descrizione |
| --- | --- | --- |
| `/api/status` | GET | Stato generale del motore |
| `/api/devices` | GET | Lista dispositivi configurati |
| `/api/config` | GET/POST | Configurazione appartamenti/gruppi/stanze |
| `/api/config/reset` | POST | Reset config a default |
| `/api/master` | GET/POST | Stato/attivazione master |
| `/api/climaoro/status` | GET | Stato dettagliato decision engine |

---

## Licenza e Responsabilità

**CLIMAORO © 2026 by UVAVIVA** · Licenza: **MIT con Condizione di Attribuzione**

**Termini**
- ✅ Attribuzione richiesta (nel codice e sui dispositivi commerciali)
- ✅ Uso commerciale permesso (con attribuzione)
- ✅ Modifiche e derivati permessi
- ✅ Uso, copia, distribuzione e vendita permessi

**Disclaimer**
Questo progetto è fornito **così com'è**, a scopo educativo e sperimentale.
- ⚠️ Non certificato per uso produttivo
- ⚠️ ⚡ **PERICOLO: gli interventi sull'impianto elettrico e termoidraulico devono essere eseguiti solo da personale qualificato**
- ⚠️ Nessuna garanzia di alcun tipo
- ⚠️ L'utente si assume ogni rischio

**Rispettare sempre le norme elettriche e di sicurezza locali.**

**Sviluppato da:** [UVAVIVA](https://github.com/UVAVIVA)

---

**Costruito con passione, dal nulla.**
