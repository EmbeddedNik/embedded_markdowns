# CLAUDE.md – Bachelor ESP32 Algorithms

## Projektkontext
Bachelorarbeit an der Hochschule Burgenland, Studiengang Angewandte Elektronik
und Photonik.

Ziel: Modulare Implementierung und Verifikation eingebetteter Steuerungs-
und Regelungsalgorithmen auf einer ESP32-Mikrocontrollerplattform.
Zusätzliche Forschungsfrage: Eignung von KI-gestützter Entwicklung
(Vibe Coding) für eingebettete Systeme.

## Plattform & Toolchain
- Hardware: ESP32 (Dual-Core Xtensa LX6, 240MHz)
- Framework: ESP-IDF (espressif/arduino-esp32 NICHT verwenden)
- Build-System: PlatformIO in VS Code
- Sprache: C (C99) – kein C++
- RTOS: FreeRTOS (in ESP-IDF integriert, kein eigener Scheduler)

## Wichtig zu Hardware-Treibern
- Für I2C, SPI, UART, GPIO ausschließlich die ESP-IDF eigenen
  Treiber verwenden (driver/i2c.h, driver/uart.h, driver/gpio.h etc.)
- Keine eigenen Low-Level Treiber implementieren
- HAL-Layer von ESP-IDF nutzen wo vorhanden

## Systemarchitektur – Dual ESP32

### esp_hardware (Slave – Smart Farm Kit)
- PlatformIO Projekt: /esp_hardware/
- Ausschließlich Hardware-Ansteuerung und Sensorauswertung
- Keine Regelungslogik, keine Entscheidungen
- Sendet Sensordaten zyklisch an esp_logic via UART2
- Empfängt Steuerbefehle von esp_logic und führt sie aus

### esp_logic (Master – Logic & Control)
- PlatformIO Projekt: /esp_logic/
- Führt alle Regelungsalgorithmen und State Machines aus
- Empfängt Sensordaten von esp_hardware via UART2
- Sendet Steuerbefehle an esp_hardware via UART2
- WiFi-fähig (UC5 reserviert)
- Keine direkte Hardware außer UART2-Bus

### UART2-Kommunikation esp_logic ↔ esp_hardware
- Interface: UART2 auf beiden ESP32s
- Baudrate: 115200
- Verkabelung (physisch festgelegt, nicht ändern):
  - esp_hardware TX = io2  →  esp_logic RX = io32
  - esp_hardware RX = io4  ←  esp_logic TX = io33
  - GND gemeinsam verbunden
- Protokoll: binäres Nachrichtenformat:
  [ START_BYTE | MSG_TYPE | PAYLOAD_LEN | PAYLOAD | CHECKSUM ]
    0xAA         1 Byte     1 Byte        N Bytes   1 Byte (XOR)
  - MSG_SENSOR_DATA  = 0x01  (esp_hardware → esp_logic, alle Sensorwerte)
  - MSG_ACTUATOR_CMD = 0x02  (esp_logic → esp_hardware, Steuerbefehle)
- Heartbeat: esp_hardware sendet zyklisch alle 100ms Sensordaten
- Timeout: kein Heartbeat in 500ms → esp_hardware geht in FAILSAFE

### FreeRTOS Task-Struktur – esp_hardware
- sensor_task    – liest alle Sensoren zyklisch (10ms)
- actuator_task  – setzt Aktorwerte aus Command-Buffer (10ms)
- comm_task      – UART2 senden/empfangen, Protokoll-Handling (5ms)
- watchdog_task  – überwacht alle Tasks, triggert Reset bei Hänger (1000ms)

### FreeRTOS Task-Struktur – esp_logic
- control_task   – PI-Regler und State Machines (10ms)
- comm_task      – UART2 senden/empfangen (5ms)
- display_task   – LCD Ausgabe (100ms)
- monitor_task   – Safety-Checks, Fehlerbehandlung (10ms)

## Hardware Pin-Mapping – esp_hardware (Smart Farm Kit)

### Aktoren
| Komponente    | Pin(s)     | Typ     | Anmerkung             |
|---------------|------------|---------|-----------------------|
| Fan           | io18, io19 | Digital | io18=IN-, io19=IN+    |
| Servo         | io26       | PWM     | Futterklappe auf/zu   |
| Water Pump    | io25       | Digital | 5V Relay, active HIGH |
| Buzzer        | io16       | Digital | active HIGH           |
| LED           | io27       | Digital | active HIGH           |
| LCD 1602      | io21, io22 | I2C     | SDA=io21, SCL=io22    |

### Sensoren
| Komponente             | Pin(s)     | Typ     | Anmerkung            |
|------------------------|------------|---------|----------------------|
| PIR Motion Sensor      | io23       | Digital | Seite Gebäude        |
| Button                 | io5        | Digital | Manueller Input      |
| Ultrasonic Module      | io12, io13 | Digital | TRIG=io12, ECHO=io13 |
| Temperature & Humidity | io17       | Digital | DHT-Protokoll        |
| Steam Sensor           | io35       | Analog  |                      |
| Photoresistor (LDR)    | io34       | Analog  | Helligkeitsmessung   |
| Water Level Sensor     | io33       | Analog  | Kapazitiv, Füllstand |
| Soil Humidity Sensor   | io32       | Analog  |                      |

### UART2 – Kommunikation zu esp_logic (festgelegt)
| Funktion        | Pin |
|-----------------|-----|
| esp_hardware TX | io2 |
| esp_hardware RX | io4 |

## Hardware Pin-Mapping – esp_logic (Master)

### UART2 – Kommunikation zu esp_hardware (festgelegt)
| Funktion      | Pin  |
|---------------|------|
| esp_logic TX  | io33 |
| esp_logic RX  | io32 |

## Architektur-Prinzipien
- Strikte Trennung in drei Schichten:
  1. HAL (Hardware Abstraction Layer) – nur IDF-Treiber, kein Algorithmus-Code
  2. Algorithm Layer – reine Regelungslogik, kein direkter Hardware-Zugriff
  3. Application Layer – verbindet HAL und Algorithmen, FreeRTOS Tasks
- Algorithmen sind hardware-unabhängig und über Interfaces testbar
- Kein malloc/free in Regelungsmodulen (statische Speicherverwaltung)
- Integer-Arithmetik bevorzugen – float nur wenn fachlich begründet
- Keine globalen Variablen außer in klar gekennzeichneten Konfigurationsdateien

## Safety-Regeln (verbindlich für jeden Code)
- Water Pump: max. kontinuierliche Laufzeit 30s, danach mind. 5s Pause
  (im actuator_task erzwingen, unabhängig von Regelungslogik)
- Alle Sensoren: Plausibilitätsprüfung – Wert außerhalb physikalisch
  sinnvollem Bereich → Fehlerflag setzen, Wert nicht weiterverwenden
- UART Timeout: kein Heartbeat in 500ms → esp_hardware setzt alle
  Aktoren in sicheren Zustand (Pumpe aus, Fan aus, Servo neutral)
- Watchdog Timer auf beiden ESP32s aktivieren und zyklisch füttern
- Bei Fehler immer: Fehlerflag setzen + LCD-Ausgabe + Logging
  (nie still ignorieren)
- Pumpe darf nie durch Software-Fehler dauerhaft aktiv bleiben –
  Abschaltung hat immer Vorrang vor Regelung

## Algorithmen im Scope
- Diskreter PI-Regler (Tustin/Bilinear-Diskretisierung)
- Finite State Machines (Zustandsautomaten)
- Interlock-Logiken
- Diagnosemechanismen / Fault Detection

## Nicht im Scope
- Entwicklung eigener Hardware-Treiber
- Entwicklung eines eigenen Schedulers (FreeRTOS verwenden)
- Optimierung von Hardwarekomponenten
- Messtechnische Analyse von Energieflüssen
- Entwicklung neuartiger Regelungsverfahren

## Coding-Konventionen (verbindlich)
- Dateinamen: snake_case → z.B. pi_controller.c, fault_detection.h
- Funktionen: snake_case mit Modul-Präfix → z.B. pi_ctrl_update(), fsm_tick()
- Typen/Structs: snake_case mit _t Suffix → z.B. pi_ctrl_t, fsm_state_t
- Konstanten und Makros: UPPER_SNAKE_CASE → z.B. PI_CTRL_OUTPUT_MAX
- Jede .c/.h Datei beginnt mit Kommentarblock:
  /*
   * @file    dateiname.c
   * @brief   Kurzbeschreibung
   * @author  Claude Code (AI) / Niklas Grill
   * @date    YYYY-MM-DD
   */
- Alle Kommentare auf Englisch

## Repo-Struktur
- /esp_hardware/  – PlatformIO Projekt Slave ESP32 (Smart Farm Kit)
- /esp_logic/     – PlatformIO Projekt Master ESP32 (Logic & Control)
- /docs/          – Dokumentation, Testprotokolle, Diagramme
- CLAUDE.md       – Kontext für Claude Code Agent
- README.md       – Projektübersicht für Menschen

## Testing
- Unit-Tests laufen auf dem Host-PC (nicht auf dem ESP32)
- Algorithmus-Layer ist so zu implementieren, dass er ohne Hardware testbar ist
- Testfälle je Modul: Sollwertsprünge, Sättigungen, Störgrößen,
  Fehlerzustände, Grenzbedingungen
- Kenngrößen: Reaktionszeit, stationäre Regelabweichung, Fehlererkennungszeit

## Arbeitsweise
- Anforderungen kommen als GitHub Issues
- Code wird ausschließlich als Pull Request eingereicht
- Kein direkter Push auf main
- Commit-Messages auf Englisch, imperative Form
  → "Add PI controller module", "Fix saturation in PI output clamping"
