@echo off
setlocal enabledelayedexpansion
title CLIMAORO Motore - Installazione guidata
color 0A

echo ============================================
echo  CLIMAORO Motore - Installazione guidata
echo ============================================
echo.
pause

:: --- 1. Controlla Python ---
echo [1/7] Controllo Python...
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo Python non trovato.
    set /p "INSTALL_PY=  Vuoi installare Python adesso? (s/n): "
    if /i "!INSTALL_PY!"=="s" (
        echo Download installer Python...
        curl.exe -L -o "%TEMP%\python_installer.exe" https://www.python.org/ftp/python/3.12.4/python-3.12.4-amd64.exe
        if !errorlevel! neq 0 (
            echo [ERRORE] Download Python fallito.
            pause
            exit /b 1
        )
        echo Installazione Python in corso...
        "%TEMP%\python_installer.exe" /quiet InstallAllUsers=1 PrependPath=1
        del "%TEMP%\python_installer.exe" 2>nul
        set "PATH=%PATH%;C:\Program Files\Python312\Scripts\;C:\Program Files\Python312\"
        echo [OK] Python installato.
    ) else (
        echo Python e necessario per installare PlatformIO.
        pause
        exit /b 1
    )
) else (
    echo [OK] Python trovato.
)
echo.

:: --- 2. Controlla PlatformIO ---
echo [2/7] Controllo PlatformIO...
where pio >nul 2>nul
if %errorlevel% neq 0 (
    echo PlatformIO non trovato.
    set /p "INSTALL_PIO=  Vuoi installare PlatformIO adesso? (s/n): "
    if /i "!INSTALL_PIO!"=="s" (
        pip install platformio
        if !errorlevel! neq 0 (
            echo [ERRORE] Installazione PlatformIO fallita.
            pause
            exit /b 1
        )
        echo [OK] PlatformIO installato.
    ) else (
        echo PlatformIO e necessario per compilare.
        pause
        exit /b 1
    )
) else (
    echo [OK] PlatformIO trovato.
)
echo.
pause

:: --- 3. Scarica il codice ---
echo [3/7] Download codice...
if not exist "climaoro-motore" (
    echo Download da GitHub...
    curl.exe -L --fail -o climaoro-motore.zip https://github.com/UVAVIVA/climaoro-motore/archive/refs/heads/main.zip
    if !errorlevel! neq 0 (
        echo.
        echo [ERRORE] Download fallito.
        echo Possibili cause:
        echo   - Nessuna connessione internet
        echo   - Il repository non e ancora pubblico
        echo   - URL non valido
        echo.
        pause
        exit /b 1
    )
    echo Estrazione file...
    powershell -Command "Expand-Archive -Path climaoro-motore.zip -DestinationPath . -Force"
    if exist "climaoro-motore-main" (
        ren climaoro-motore-main climaoro-motore
    )
    del climaoro-motore.zip 2>nul
    echo [OK] Codice scaricato.
) else (
    echo [OK] Codice gia presente.
)
echo.

:: --- 4. Entra nella cartella ---
if not exist "climaoro-motore" (
    echo [ERRORE] Cartella non trovata dopo il download.
    pause
    exit /b 1
)
cd /d "climaoro-motore"
pause

:: --- 5. Chiedi dati WiFi ---
echo [4/7] Configurazione WiFi e Rete.
echo.
set /p "WIFI_SSID=  SSID (nome rete): "
set /p "WIFI_PASS=  Password WiFi: "
set /p "MOTORE_IP=  IP del motore (es. 192.168.1.196): "
set /p "MOTORE_GW=  Gateway (es. 192.168.1.1): "
set /p "MOTORE_NM=  Netmask (es. 255.255.255.0): "

(
echo #pragma once
echo.
echo #define WIFI_SSID1 "!WIFI_SSID!"
echo #define WIFI_PASS1 "!WIFI_PASS!"
echo #define WIFI_SSID2 ""
echo #define WIFI_PASS2 ""
echo.
echo #define MOTORE_IP      "!MOTORE_IP!"
echo #define MOTORE_GATEWAY "!MOTORE_GW!"
echo #define MOTORE_NETMASK "!MOTORE_NM!"
) > src\secrets.h
echo [OK] Configurazione WiFi salvata.
echo.

:: --- 6. Copia file termostati di default ---
echo [5/7] Creazione file termostati (modificabili dall'app)...
echo.
if not exist "src\devices.c" (
    copy "src\devices_example.c" "src\devices.c" >nul
    echo [OK] File termostati creato dal template.
) else (
    echo [OK] File termostati gia presente.
)
echo.

:: --- 7. Compilazione ---
echo [6/7] Compilazione in corso...
echo.
pio run
if %errorlevel% neq 0 (
    echo.
    echo [ERRORE] Compilazione fallita.
    pause
    exit /b 1
)
echo.
echo [OK] Compilazione completata.
echo.

:: --- 8. Upload ---
echo [7/7] Caricamento firmware sull'ESP32...
echo.
pio run -t upload
if %errorlevel% neq 0 (
    echo.
    echo [ERRORE] Caricamento fallito.
    pause
    exit /b 1
)
echo.
echo ============================================
echo  Installazione completata con successo!
echo ============================================
echo.

:: --- 9. Monitor seriale ---
echo Avvio monitor seriale...
echo (premi Ctrl+C per uscire)
echo.
pio device monitor

pause


