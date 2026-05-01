# Anforderungen – Bachelor ESP32 Smart Farm

## Projektübersicht

Bachelorarbeit an der FH Burgenland, Studiengang Angewandte Elektronik und Photonik.  
Hardware-Plattform: Keyestudio ESP32 Smart Farm Kit + zweiter ESP32 als Logic-Controller.  
Forschungsfrage: Modulare Implementierung eingebetteter Steuerungs- und Regelungsalgorithmen
sowie Eignung von KI-gestützter Entwicklung (Vibe Coding) für eingebettete Systeme.

---

## Systemarchitektur

```
┌─────────────────────────────┐         ┌─────────────────────────────┐
│        esp_hardware         │  UART2  │         esp_logic           │
│   (Slave – Smart Farm Kit)  │◄───────►│   (Master – Logic & Control)│
│                             │         │                             │
│  - Alle Sensoren lesen      │         │  - Zustandsmaschinen        │
│  - Alle Aktoren ansteuern   │         │  - Klimasteuerung           │
│  - Keine Regelungslogik     │         │  - Safety-Monitoring        │
│  - Sensordaten senden       │         │  - Alarmlogik               │
│  - Befehle empfangen        │         │  - Profilbasierte Steuerung │
└─────────────────────────────┘         └─────────────────────────────┘

UART2 Verkabelung (physisch festgelegt):
  esp_hardware TX = io2  →  esp_logic RX = io32
  esp_hardware RX = io4  ←  esp_logic TX = io33
  GND gemeinsam
```

---

## Hardware Pin-Mapping – esp_hardware (Smart Farm Kit)

### Aktoren
| Komponente    | Pin(s)     | Typ     | Anmerkung                       |
|---------------|------------|---------|---------------------------------|
| Fan           | io18, io19 | PWM     | io18=LOW (Richtung), io19=PWM   |
| Servo         | io26       | PWM     | Klappe: 0=geschlossen, 100=offen|
| Water Pump    | io25       | Digital | 5V Relay, active HIGH           |
| Buzzer        | io16       | Digital | active HIGH                     |
| LED           | io27       | Digital | active HIGH                     |
| LCD 1602      | io21, io22 | I2C     | SDA=io21, SCL=io22              |

### Sensoren
| Komponente             | Pin(s)     | Typ     | Anmerkung            |
|------------------------|------------|---------|----------------------|
| PIR Motion Sensor      | io23       | Digital | Bewegungserkennung   |
| Button                 | io5        | Digital | Manueller Input      |
| Ultrasonic Module      | io12, io13 | Digital | TRIG=io12, ECHO=io13 |
| Temperature & Humidity | io17       | Digital | DHT-Protokoll        |
| Photoresistor (LDR)    | io34       | Analog  | Helligkeitsmessung   |
| Water Level Sensor     | io33       | Analog  | Kapazitiv, Füllstand |

### UART2 – Kommunikation zu esp_logic
| Funktion        | Pin |
|-----------------|-----|
| esp_hardware TX | io2 |
| esp_hardware RX | io4 |

## Hardware Pin-Mapping – esp_logic (Master)

### UART2 – Kommunikation zu esp_hardware
| Funktion      | Pin  |
|---------------|------|
| esp_logic TX  | io33 |
| esp_logic RX  | io32 |

---

## Technische Rahmenbedingungen (für alle UCs gültig)

- Framework: ESP-IDF (kein Arduino)
- Sprache: C (C99), kein C++
- Build: PlatformIO in VS Code
- RTOS: FreeRTOS (in ESP-IDF integriert)
- Kein `malloc`/`free` in Steuerungsmodulen
- Integer-Arithmetik bevorzugen, `float` nur wenn fachlich nötig
- Alle IDF-eigenen Treiber verwenden (`driver/uart.h`, `driver/gpio.h`, `driver/i2c.h` etc.)
- Kommentare auf Englisch
- Dateinamen: `snake_case`
- Funktionen: `snake_case` mit Modul-Präfix (z. B. `control_task_init()`)

### Safety-Regeln (verbindlich überall)
- **Water Pump:** max. 30 s kontinuierlich ON, danach min. 5 s Pause (im `actuator_task` erzwingen)
- **Alle Sensoren:** Plausibilitätsprüfung; bei ungültigem Wert Fehlerflag setzen
- **UART Timeout:** kein Frame in 500 ms → esp_hardware in FAILSAFE (alle Aktoren aus)
- **Watchdog Timer** auf beiden ESP32s aktiv (TWDT, Timeout 5000 ms)
- Bei Fehler: Fehlerflag + LCD-Ausgabe + Logging (nie still ignorieren)

---

## UC1 – Kommunikationsschicht & Systemgrundstruktur

### Ziel
Aufbau der gesamten Software-Infrastruktur für beide ESP32s:
FreeRTOS Task-Struktur, UART2-Kommunikationsprotokoll, gemeinsame Datenstrukturen.
UC1 bildet die Basis für alle weiteren Use Cases.

---

### UC1.1 – Shared Protocol Types

**Was:** Gemeinsame Header-Dateien mit allen Protokoll-Definitionen (identisch auf beiden ESP32s).

**Protokoll-Frame:**
```
[ 0xAA | MSG_TYPE | PAYLOAD_LEN | PAYLOAD (max 32 Bytes) | CRC-8 ]
   1 B      1 B        1 B           0–32 B                  1 B
```

**Message Types:**
| Konstante          | Wert   | Richtung                        |
|--------------------|--------|---------------------------------|
| `MSG_SENSOR_DATA`  | `0x01` | esp_hardware → esp_logic        |
| `MSG_ACTUATOR_CMD` | `0x02` | esp_logic → esp_hardware        |
| `MSG_HEARTBEAT`    | `0x03` | esp_hardware → esp_logic (leer) |
| `MSG_ACK`          | `0x04` | beide Richtungen (leer)         |

**Checksumme:** CRC-8, Polynom `0x07`, Startwert `0x00`, berechnet über `MSG_TYPE`, `PAYLOAD_LEN` und alle Payload-Bytes. `START_BYTE` (`0xAA`) ist nicht Teil der Checksumme.

**Sensor Data Payload – `sensor_data_t`** (13 Bytes, `__attribute__((packed))`):

| Feld             | Typ        | Größe  | Einheit / Kodierung                    | Pin      |
|------------------|------------|--------|----------------------------------------|----------|
| `water_level`    | `uint16_t` | 2 B    | ADC-Rohwert 0–4095                     | io33     |
| `photoresistor`  | `uint16_t` | 2 B    | ADC-Rohwert 0–4095                     | io34     |
| `temperature`    | `int16_t`  | 2 B    | °C × 10 (z. B. 235 = 23,5 °C)         | io17     |
| `humidity`       | `uint16_t` | 2 B    | % × 10 (z. B. 654 = 65,4 %)           | io17     |
| `ultrasonic_mm`  | `uint16_t` | 2 B    | Distanz in mm, 0–4000                  | io12/13  |
| `pir_detected`   | `uint8_t`  | 1 B    | 0 = keine Bewegung, 1 = Bewegung       | io23     |
| `button_pressed` | `uint8_t`  | 1 B    | 0 = nicht gedrückt, 1 = gedrückt      | io5      |
| `error_flags`    | `uint8_t`  | 1 B    | Bitmask, siehe Tabelle unten           | —        |

**`error_flags` Bitmask:**
| Bit | Maske  | Konstante                 | Bedeutung                     |
|-----|--------|---------------------------|-------------------------------|
| 0   | `0x01` | `ERROR_FLAG_LDR`          | Photoresistor ADC-Fehler      |
| 1   | `0x02` | `ERROR_FLAG_WATER_LEVEL`  | Wasserstand ADC-Fehler        |
| 2   | —      | —                         | Reserviert                    |
| 3   | `0x08` | `ERROR_FLAG_DISTANCE`     | Ultraschall-Timeout           |
| 4   | `0x10` | `ERROR_FLAG_TEMPERATURE`  | DHT Temperatur-Fehler         |
| 5   | `0x20` | `ERROR_FLAG_HUMIDITY`     | DHT Luftfeuchte-Fehler        |
| 6   | —      | —                         | Reserviert                    |
| 7   | `0x80` | `ERROR_FLAG_UART_TIMEOUT` | UART Kommunikations-Timeout   |

**Actuator Command Payload – `actuator_cmd_t`** (7 Bytes):

| Feld             | Typ       | Größe | Beschreibung                              | Pin     |
|------------------|-----------|-------|-------------------------------------------|---------|
| `pump_on`        | `uint8_t` | 1 B   | 0 = aus, 1 = an                           | io25    |
| `fan_speed`      | `uint8_t` | 1 B   | 0 = aus, >0 = an (PWM-Wert)               | io19    |
| `servo_position` | `uint8_t` | 1 B   | 0 = geschlossen, 100 = offen              | io26    |
| `led_on`         | `uint8_t` | 1 B   | 0 = aus, 1 = an                           | io27    |
| `buzzer_on`      | `uint8_t` | 1 B   | 0 = aus, 1 = an                           | io16    |
| `display_mode`   | `uint8_t` | 1 B   | 0 = OK, 1 = REFILL                        | I2C     |
| `profile`        | `uint8_t` | 1 B   | 0 = ECO, 1 = NORMAL, 2 = PERFORMANCE      | —       |

**Dateien:** `protocol.h` (in beiden Projekten identisch, mit Compile-Time-Assertions)

---

### UC1.2 – FreeRTOS Grundstruktur esp_hardware

**Tasks:**
| Task           | Priorität | Periode  | Stack |
|----------------|-----------|----------|-------|
| sensor_task    | 3         | 10 ms    | 4096  |
| actuator_task  | 3         | 10 ms    | 4096  |
| comm_task      | 4         | 5 ms     | 4096  |
| watchdog_task  | 5         | 1000 ms  | 2048  |

**`sensor_task`:**
- Liest alle Sensoren zyklisch via ESP-IDF ADC/GPIO-Treiber
- DHT-Protokoll für Temperatur/Luftfeuchtigkeit auf io17
- Ultraschall: Trigger io12, Echo io13, Distanz per `esp_timer_get_time()`
- Plausibilitätsprüfung auf alle Werte; Fehlerflag bei ungültigem Wert
- Debug-Ausgabe via `ESP_LOGI` alle 500 ms

**`actuator_task`:**
- GPIO-Outputs und PWM (LEDC-Treiber) initialisieren
- Servo io26 und Fan io18/io19 per PWM ansteuern
- Pump Safety: max 30 s ON, min 5 s Pause – unabhängig vom Befehl
- Befehle aus FreeRTOS-Queue lesen und ausführen

**`comm_task`:**
- UART2 initialisieren (TX=io2, RX=io4, 115200 Baud, 8N1)
- Sensordaten alle 100 ms als `MSG_SENSOR_DATA` senden
- Eingehende `MSG_ACTUATOR_CMD` empfangen und in Queue schreiben
- Timeout-Erkennung: kein Befehl in 500 ms → FAILSAFE (alle Aktoren aus)

**`watchdog_task`:**
- ESP-IDF Task Watchdog Timer (TWDT, Timeout 5000 ms) initialisieren
- Alle Tasks subscriben, Stack High Water Mark alle 10 s loggen

---

### UC1.3 – FreeRTOS Grundstruktur esp_logic

**Tasks:**
| Task           | Priorität | Periode  | Stack |
|----------------|-----------|----------|-------|
| control_task   | 4         | 10 ms    | 4096  |
| comm_task      | 5         | 10 ms    | 4096  |
| serial_task    | 3         | 20 ms    | 3072  |
| display_task   | 2         | 100 ms   | 4096  |
| monitor_task   | 4         | 10 ms    | 3072  |
| watchdog_task  | 6         | 1000 ms  | 2048  |

**`comm_task`:**
- UART2 initialisieren (TX=io33, RX=io32, 115200 Baud, 8N1)
- Eingehende `MSG_SENSOR_DATA` empfangen → `sensor_data_t` befüllen
- Ausgehende `MSG_ACTUATOR_CMD` aus Queue senden
- Heartbeat-Timeout überwachen (500 ms)

**`control_task`:**
- Sensor-Daten lesen (via Mutex geschütztes Shared-Struct)
- Zustandsmaschinen ausführen (UC2, UC3)
- Steuerbefehle als `actuator_cmd_t` in Queue schreiben

**`serial_task`:**
- USB/UART0-Eingabe des Betreibers
- Profilwechsel (ECO/NORMAL/PERFORMANCE) via Tastatureingabe
- Schreibt aktives Profil in globale Variable `g_active_profile`

**`display_task`:**
- LCD 1602 via I2C (SDA=io21, SCL=io22 am esp_hardware)
- Anzeige von Systemzustand, Messwerten und Warnungen
- Empfängt `display_mode` aus `actuator_cmd_t` (0=OK, 1=REFILL)

**`monitor_task`:**
- Sensor-Daten auf Aktualität prüfen (älter als 600 ms → Warnung)
- Safety-Checks ausführen und Fehlerzustände melden

---

## UC2 – Wasserstandsüberwachung & Profilbasierte Systemsteuerung

### Ziel
Kontinuierliche Überwachung des Wasserstands mit automatischer Zustandserkennung und
Alarmierung bei niedrigem Füllstand. Parallel dazu ermöglicht ein dreistufiges Profilsystem
(ECO / NORMAL / PERFORMANCE) eine adaptierbare Steuerung der Aktoren je nach Betriebsanforderung.
Beide Mechanismen laufen im `control_task` auf dem esp_logic.

---

### UC2.1 – Wasserstand-Zustandsmaschine

Der Wasserstand wird kontinuierlich per ADC ausgewertet. Unterschreitet der Füllstand einen
definierten Schwellwert, wechselt das System in den REFILL-Zustand und alarmiert den Betreiber.
Eine Hysterese verhindert Flattern an der Zustandsgrenze.

**Zustandsdiagramm:**
```
              level ≤ WATER_REFILL_ENTER (1100 ADC)
WATER_OK ────────────────────────────────────────► WATER_REFILL
  ▲                                                      │
  └──────── level > WATER_REFILL_EXIT (1110 ADC) ────────┘
```

**Zustände:**

| Zustand            | Bedingung               | Pumpe     | Display  | Buzzer       |
|--------------------|-------------------------|-----------|----------|--------------|
| `WATER_STATE_OK`   | Füllstand > 1110 ADC    | steuerbar | OK       | aus          |
| `WATER_STATE_REFILL` | Füllstand ≤ 1100 ADC  | gesperrt  | REFILL   | Alarmton     |

**Schwellwerte und Hysterese:**
| Konstante            | Wert | Bedeutung                           |
|----------------------|------|-------------------------------------|
| `WATER_REFILL_ENTER` | 1100 | OK → REFILL wenn `level ≤ 1100`     |
| `WATER_REFILL_EXIT`  | 1110 | REFILL → OK wenn `level > 1110`     |

**Anforderungen:**
- Zustandswechsel nur per Hysterese (kein Flattern bei Grenzwerten)
- Jeder Zustandswechsel wird via `ESP_LOGI` geloggt
- Aktueller Zustand auf LCD angezeigt

---

### UC2.2 – Profilbasierte Aktorsteuerung

Der Betreiber kann via serielle Konsole (USB/UART0) zwischen drei Systemprofilen umschalten,
die den Ressourceneinsatz – insbesondere die Lüfteraktivität – an den jeweiligen Betriebsbedarf
anpassen. Das Profil wird bei jedem Steuerzyklus als `actuator_cmd_t.profile` an den
esp_hardware übertragen.

**Profile und Lüfter-Temperaturschwellen:**
| Profil        | `profile`-Wert | Lüfter LOW ab | Lüfter HIGH ab | Einsatzszenario               |
|---------------|----------------|---------------|----------------|-------------------------------|
| `ECO`         | 0              | 28,0 °C       | 32,0 °C        | Energiesparbetrieb            |
| `NORMAL`      | 1              | 25,0 °C       | 28,0 °C        | Standardbetrieb               |
| `PERFORMANCE` | 2              | 23,0 °C       | 26,0 °C        | Maximale Klimatisierung       |

Das aktive Profil wird in der globalen Variable `g_active_profile` (`system_profile_t`) gehalten.

---

## UC3 – Klimasteuerung & Alarmanlage

### Ziel
Automatische Klimasteuerung basierend auf Temperatur und Lichtverhältnissen.
Zusätzlich eine sensorgestützte Alarmanlage, die bei Bewegungserkennung im Dunkeln auslöst.
Alle Steuerungen laufen als unabhängige Zustandsmaschinen im `control_task`.

---

### UC3.1 – Klimasteuerung

**Lüftersteuerung (profilabhängig):**

Die Temperaturschwellen werden durch das aktive Systemprofil (UC2.2) bestimmt:

| Bedingung                                        | Lüfterzustand |
|--------------------------------------------------|---------------|
| Temperatur ≥ `fan_high_d10` (profilabhängig)     | HIGH          |
| `fan_low_d10` ≤ Temperatur < `fan_high_d10`      | LOW           |
| Temperatur < `fan_low_d10` (profilabhängig)      | OFF           |

**Licht- & Servosteuerung (LDR-basiert):**

Der Helligkeitssensor steuert den Tag/Nacht-Zustand des Systems. Ein Kandidatenstatus muss
mindestens 1000 ms stabil anliegen, bevor der Übergang ausgeführt wird (Entprellzeit).

```
              LDR ≤ 1100 ADC für 1000 ms
LIGHT_DAY ─────────────────────────────► LIGHT_NIGHT
  ▲                                           │
  └───── LDR > 1300 ADC für 1000 ms ─────────┘
```

| Zustand            | Servo-Position  | Anmerkung                     |
|--------------------|-----------------|-------------------------------|
| `LIGHT_STATE_DAY`  | 0 (geschlossen) | Normalbetrieb                 |
| `LIGHT_STATE_NIGHT`| 100 (offen)     | Nachtmodus, Alarm aktiv       |

**Button (io5):**
- Kurz drücken (< 1 s): Display-Modus wechseln (zeigt verschiedene Sensorwerte)

---

### UC3.2 – Nacht-Alarmanlage

Der PIR-Sensor (io23) löst einen Alarm aus, wenn das System im Nacht-Zustand
(`LIGHT_STATE_NIGHT`) eine Bewegung erkennt. Eine Schutzzeit nach dem Nacht-Übergang
verhindert Fehlalarme durch die Servobewegung.

**Auslösebedingungen:**
- Systemzustand: `LIGHT_STATE_NIGHT`
- Schutzzeit abgelaufen: ≥ 5000 ms nach letztem NIGHT-Übergang
- PIR-Sensor (io23): Bewegung erkannt

**Alarmverhalten:**
| Aktor   | Verhalten                              |
|---------|----------------------------------------|
| Buzzer  | Dauerton                               |
| LED     | schnelles Blinken                      |
| Display | Alarmstatus                            |

**Zeitkonstanten:**
| Konstante              | Wert     | Bedeutung                                     |
|------------------------|----------|-----------------------------------------------|
| `LDR_NIGHT_ENTER_ADC`  | 1100     | LDR-Schwellwert für Nacht-Erkennung           |
| `LDR_DAY_ENTER_ADC`    | 1300     | LDR-Schwellwert für Tag-Erkennung             |
| `LDR_CONFIRM_MS`       | 1000 ms  | Kandidatenstatus muss 1 s stabil sein         |
| `NIGHT_ALARM_GUARD_MS` | 5000 ms  | Sperrzeit nach NIGHT-Transition               |
| `NIGHT_ALARM_HOLD_MS`  | 5000 ms  | Alarm bleibt min. 5 s aktiv nach Auslösung   |

**Anforderungen:**
- Klimasteuerung und Alarmanlage laufen als unabhängige Zustandsmaschinen
- Jeder Zustandswechsel wird via `ESP_LOGI` geloggt
- Hysterese und Bestätigungszeiten bei allen Schwellwerten

---

## Softwaretests

### Ziel
Sicherstellung der Korrektheit kritischer Steuerungsmodule durch automatisierte Unit-Tests.
Die Tests sind **host-seitig ausführbar** (kein ESP32 erforderlich) und basieren auf dem
[Unity Test Framework](https://github.com/ThrowTheSwitch/Unity).
Kritische Logik – Protokoll-Parser und Zustandsmaschinen – wurde dazu in reine C-Module
ohne ESP-IDF-Abhängigkeiten ausgelagert (`proto_utils.c`, `control_logic.c`).

### Testausführung

```bash
cd esp_logic
pio test -e native
```

Tests werden mit dem nativen Host-Compiler gebaut und ausgeführt (kein Flash nötig).

---

### test_protocol (6 Tests)

Testet die CRC-8-Checksumme und den UART-Frame-Parser in `proto_utils.c`.

| Test                                       | Prüft                                                     |
|--------------------------------------------|-----------------------------------------------------------|
| `test_checksum_is_deterministic`           | Gleicher Input liefert immer gleiche CRC                  |
| `test_checksum_changes_with_different_input` | Unterschiedliche Payloads → unterschiedliche CRC        |
| `test_parser_accepts_valid_heartbeat_frame`| Gültiger Heartbeat-Frame wird korrekt akzeptiert          |
| `test_parser_rejects_wrong_checksum`       | Frame mit falscher CRC wird abgelehnt                     |
| `test_parser_rejects_oversized_payload_length` | Payload-Länge > 32 Bytes wird abgelehnt              |
| `test_parser_resyncs_after_garbage_bytes`  | Parser findet Start-Byte nach Garbage-Bytes wieder        |

---

### test_control_logic (9 Tests)

Testet die Zustandsmaschinen und die Lüfterberechnung in `control_logic.c`.

| Testgruppe            | Anzahl | Prüft                                                      |
|-----------------------|--------|------------------------------------------------------------|
| Wasser-FSM            | 4      | Schwellwertübergänge OK↔REFILL, Hysterese-Verhalten        |
| Licht-FSM             | 3      | Tag/Nacht-Übergänge mit Hysterese und Bestätigungslogik    |
| Lüftergeschwindigkeit | 2      | Temperaturbasierte Berechnung über alle drei Profile        |

---

## Repo-Struktur

```
/embedded_markdowns/
├── esp_hardware/          PlatformIO Projekt – Slave ESP32
│   ├── src/
│   │   ├── main.c
│   │   ├── sensor_task.c
│   │   ├── actuator_task.c
│   │   ├── comm_task.c        # enthält CRC-8 und Frame-Parser (lokal)
│   │   └── watchdog_task.c
│   ├── include/
│   │   ├── protocol.h         # geteilt mit esp_logic (identisch)
│   │   ├── sensor_task.h
│   │   ├── actuator_task.h
│   │   ├── comm_task.h
│   │   └── task_config.h
│   └── platformio.ini
├── esp_logic/             PlatformIO Projekt – Master ESP32
│   ├── src/
│   │   ├── main.c
│   │   ├── control_task.c     # UC2 + UC3 Zustandsmaschinen
│   │   ├── control_logic.c    # pure Steuerungslogik, host-testbar
│   │   ├── comm_task.c
│   │   ├── proto_utils.c      # CRC-8 und Frame-Parser, host-testbar
│   │   ├── display_task.c
│   │   ├── monitor_task.c
│   │   ├── serial_task.c      # Profilwechsel via USB/UART0
│   │   └── watchdog_task.c
│   ├── include/
│   │   ├── protocol.h         # geteilt mit esp_hardware (identisch)
│   │   ├── control_task.h     # system_profile_t, g_active_profile
│   │   ├── control_logic.h    # water_state_t, light_state_t
│   │   ├── proto_utils.h      # calc_checksum()
│   │   ├── comm_task.h
│   │   ├── display_task.h
│   │   ├── monitor_task.h
│   │   ├── serial_task.h
│   │   └── task_config.h      # Schwellwerte, Task-Parameter
│   ├── test/
│   │   ├── test_protocol/
│   │   │   └── test_protocol.c
│   │   └── test_control_logic/
│   │       └── test_control_logic.c
│   └── platformio.ini
├── docs/
│   ├── protocol/
│   │   └── uart_protocol.md
│   └── requirements/
│       └── REQUIREMENTS.md
├── CLAUDE.md
└── README.md
```

---

## Reihenfolge der Umsetzung

```
UC1.1 → Shared Protocol Types (protocol.h, proto_utils.c/h)
UC1.2 → esp_hardware FreeRTOS Grundstruktur
UC1.3 → esp_logic FreeRTOS Grundstruktur
UC1.4 → UART2 Kommunikation (beide Seiten)
UC2.1 → Wasserstand-Zustandsmaschine
UC2.2 → Profilbasierte Aktorsteuerung (serial_task, g_active_profile)
UC3.1 → Klimasteuerung (Lüfter profilabhängig, LDR, Servo)
UC3.2 → Nacht-Alarmanlage (PIR, Buzzer, LED)
Tests → Unity Unit-Tests für protocol und control_logic (pio test -e native)
```
