# Requirements – Bachelor ESP32 Smart Farm

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
│  - Alle Sensoren lesen      │         │  - PI-Regler                │
│  - Alle Aktoren ansteuern   │         │  - State Machines           │
│  - Keine Regelungslogik     │         │  - Safety-Monitoring        │
│  - Sensordaten senden       │         │  - Lastmanagement           │
│  - Befehle empfangen        │         │  - Alarmlogik               │
└─────────────────────────────┘         └─────────────────────────────┘

UART2 Verkabelung (physisch festgelegt):
  esp_hardware TX = io2  →  esp_logic RX = io32
  esp_hardware RX = io4  ←  esp_logic TX = io33
  GND gemeinsam
```

---

## Hardware Pin-Mapping – esp_hardware (Smart Farm Kit)

### Aktoren
| Komponente    | Pin(s)     | Typ     | Anmerkung                      |
|---------------|------------|---------|--------------------------------|
| Fan           | io18, io19 | Digital | io18=IN-, io19=IN+             |
| Servo         | io26       | PWM     | Futterklappe auf/zu            |
| Water Pump    | io25       | Digital | 5V Relay, active HIGH          |
| Buzzer        | io16       | Digital | active HIGH                    |
| LED           | io27       | Digital | active HIGH                    |
| LCD 1602      | io21, io22 | I2C     | SDA=io21, SCL=io22             |

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
- Kein malloc/free in Regelungsmodulen
- Integer-Arithmetik bevorzugen, float nur wenn fachlich nötig
- Alle IDF-eigenen Treiber verwenden (driver/uart.h, driver/gpio.h, driver/i2c.h etc.)
- Kommentare auf Englisch
- Dateinamen: snake_case
- Funktionen: snake_case mit Modul-Präfix (z.B. pi_ctrl_update())

### Safety-Regeln (verbindlich überall)
- Water Pump: max. 30s kontinuierlich ON, danach min. 5s Pause (im actuator_task erzwingen)
- Alle Sensoren: Plausibilitätsprüfung, bei ungültigem Wert Fehlerflag setzen
- UART Timeout: kein Heartbeat in 500ms → esp_hardware in FAILSAFE (alle Aktoren aus)
- Watchdog Timer auf beiden ESP32s aktiv
- Bei Fehler: Fehlerflag + LCD-Ausgabe + Logging (nie still ignorieren)

---

## UC1 – Kommunikationsschicht & Systemgrundstruktur

### Ziel
Aufbau der gesamten Software-Infrastruktur für beide ESP32s:
FreeRTOS Task-Struktur, UART2-Kommunikationsprotokoll, gemeinsame Datenstrukturen.
Das ist die Basis für alle weiteren Use Cases.

### UC1.1 – Shared Protocol Types
**Was:** Gemeinsame Header-Dateien mit allen Protokoll-Definitionen.

**Protokoll-Frame:**
```
[ 0xAA | MSG_TYPE | PAYLOAD_LEN | PAYLOAD (max 32 Bytes) | CHECKSUM (XOR) ]
```

**Message Types:**
- MSG_SENSOR_DATA  = 0x01  (esp_hardware → esp_logic)
- MSG_ACTUATOR_CMD = 0x02  (esp_logic → esp_hardware)
- MSG_HEARTBEAT    = 0x03  (esp_hardware → esp_logic, leer)
- MSG_ACK          = 0x04  (beide Richtungen)

**Sensor Data Payload** (alle Werte integer, kein float):
```c
typedef struct {
    uint16_t water_level;    // ADC 0-4095 (io33)
    uint16_t soil_humidity;  // ADC 0-4095 (io32)
    uint16_t photoresistor;  // ADC 0-4095 (io34)
    uint16_t steam_sensor;   // ADC 0-4095 (io35)
    int16_t  temperature;    // °C * 10 (z.B. 235 = 23.5°C)
    uint16_t humidity;       // % * 10 (z.B. 654 = 65.4%)
    uint16_t ultrasonic_mm;  // Distanz in mm
    uint8_t  pir_detected;   // 0 oder 1
    uint8_t  button_pressed; // 0 oder 1
    uint8_t  error_flags;    // Bitmask: bit0=sensor_fail, bit1=uart_timeout
} sensor_data_t;
```

**Actuator Command Payload:**
```c
typedef struct {
    uint8_t pump_on;         // 0=aus, 1=an (io25)
    uint8_t fan_speed;       // 0=aus, 1=low, 2=high (io18/io19)
    uint8_t servo_position;  // 0=geschlossen, 100=offen (io26)
    uint8_t led_on;          // 0 oder 1 (io27)
    uint8_t buzzer_on;       // 0 oder 1 (io16)
    uint8_t display_mode;    // 0=normal, 1=warning, 2=critical, 3=alarm
} actuator_cmd_t;
```

**Dateien:** protocol.h (in beiden Projekten identisch)

---

### UC1.2 – FreeRTOS Grundstruktur esp_hardware

**Tasks:**
| Task           | Priorität | Periode | Stack |
|----------------|-----------|---------|-------|
| sensor_task    | 3         | 10ms    | 4096  |
| actuator_task  | 3         | 10ms    | 4096  |
| comm_task      | 4         | 5ms     | 4096  |
| watchdog_task  | 5         | 1000ms  | 2048  |

**sensor_task:**
- Liest alle Sensoren zyklisch via ESP-IDF ADC/GPIO/LEDC Treiber
- DHT-Protokoll für Temperatur/Luftfeuchtigkeit auf io17
- Ultraschall: Trigger io12, Echo io13, Distanz per esp_timer_get_time()
- Plausibilitätsprüfung auf alle Werte
- Debug-Ausgabe via ESP_LOGI alle 500ms

**actuator_task:**
- GPIO-Outputs initialisieren
- PWM via LEDC-Treiber für Servo (io26) und Fan (io18/io19)
- Pump Safety: max 30s ON, min 5s Pause – unabhängig vom Befehl
- Befehle aus FreeRTOS Queue lesen und ausführen

**comm_task:**
- UART2 initialisieren (TX=io2, RX=io4, 115200 Baud)
- Sensordaten alle 100ms als MSG_SENSOR_DATA senden
- Eingehende MSG_ACTUATOR_CMD empfangen und in Queue schreiben
- Timeout-Erkennung: kein Befehl in 500ms → FAILSAFE

**watchdog_task:**
- ESP-IDF Task Watchdog Timer (TWDT) initialisieren
- Alle Tasks subscriben
- Stack High Water Mark alle 10s loggen

---

### UC1.3 – FreeRTOS Grundstruktur esp_logic

**Tasks:**
| Task           | Priorität | Periode | Stack |
|----------------|-----------|---------|-------|
| control_task   | 4         | 10ms    | 4096  |
| comm_task      | 5         | 5ms     | 4096  |
| display_task   | 2         | 100ms   | 4096  |
| monitor_task   | 4         | 10ms    | 3072  |
| watchdog_task  | 6         | 1000ms  | 2048  |

**comm_task:**
- UART2 initialisieren (TX=io33, RX=io32, 115200 Baud)
- Eingehende MSG_SENSOR_DATA empfangen → sensor_data_t Struct befüllen
- Ausgehende MSG_ACTUATOR_CMD aus Queue senden
- Heartbeat-Timeout überwachen

**control_task:**
- Sensor-Daten lesen (via Mutex geschütztes Shared-Struct)
- Regelungsalgorithmen ausführen (UC2, UC3, UC4)
- Steuerbefehle als actuator_cmd_t in Queue schreiben

**display_task:**
- LCD 1602 via I2C (ESP-IDF driver/i2c.h, SDA=io21, SCL=io22 – am esp_hardware)
- Display-Befehle via actuator_cmd_t.display_mode empfangen
- Systemzustand, Messwerte, Warnungen anzeigen

**monitor_task:**
- Sensor-Daten auf Aktualität prüfen (älter als 600ms → Warnung)
- Safety-Checks ausführen
- Fehlerzustände an display_task und control_task melden

---

## UC2 – Füllstandsregelung (PI-Regler)

### Ziel
Diskreter PI-Regler hält einen vorgegebenen Sollwert des Wasserstands im Becken.
Die Wasserpumpe pumpt Wasser ab wenn der Füllstand zu hoch ist.
Nachfüllen erfolgt manuell durch den Bediener.

### Regelstrecke
- **Sensor:** Water Level Sensor, io33, ADC-Wert 0-4095
- **Aktor:** Water Pump, io25, Relay (an/aus)
- **Regelgröße:** Füllstand (ADC-Wert)
- **Stellgröße:** Pumpe an/aus (binär) oder PWM (0-100%)
- **Sollwert:** Fix einstellbar (z.B. ADC-Wert 2000)

### Wichtige Anforderungen
- Kein Überschwingen nach unten (Pumpe nicht zu aggressiv)
- Anti-Windup: Integrator einfrieren wenn Stellgröße saturiert
- Pumpe ist einseitig: kann nur abpumpen, nicht nachfüllen
- Fehlervorzeichen: measured > setpoint → Pumpe AN (Wasser zu hoch)
- Pump Safety Rule aus UC1 gilt: max 30s ON, min 5s Pause

### PI-Algorithmus (Tustin-Diskretisierung)
```
u[k] = u[k-1] + Kp*(e[k]-e[k-1]) + Ki*(Ts/2)*(e[k]+e[k-1])

e[k]  = measured_value - setpoint  (positiv wenn zu voll)
u[k]  = Pumpen-Ausgabe (0=aus, 100=voll an)
Ts    = Abtastzeit in Sekunden
```

### Structs
```c
typedef struct {
    int32_t kp;              // Kp * 1000 (fixed-point)
    int32_t ki;              // Ki * 1000 (fixed-point)
    int32_t sample_time_ms;
    int32_t output_min;      // 0 (Pumpe aus)
    int32_t output_max;      // 100 (Pumpe voll an)
    int32_t setpoint;
    int32_t error_prev;
    int32_t output_prev;
    uint8_t anti_windup;
    uint8_t initialized;
} pi_ctrl_t;
```

### API
```c
void    pi_ctrl_init(pi_ctrl_t *ctrl, int32_t kp, int32_t ki,
                     int32_t sample_time_ms, int32_t out_min, int32_t out_max);
void    pi_ctrl_set_setpoint(pi_ctrl_t *ctrl, int32_t setpoint);
int32_t pi_ctrl_update(pi_ctrl_t *ctrl, int32_t measured_value);
void    pi_ctrl_reset(pi_ctrl_t *ctrl);
```

### Unit Tests (host-seitig, kein ESP32 nötig)
- Sollwertsprung: measured > setpoint → Pumpe geht an
- Sollwert erreicht: Pumpe geht aus, keine Unterschwingung
- Anti-Windup: Integrator friert ein bei Sättigung
- Reset: interner Zustand wird korrekt gelöscht

---

## UC3 – Lastmanagement & Ressourcensteuerung

### Ziel
Finite State Machine mit drei Zustandsebenen basierend auf dem Wasserstand.
Lasten werden je nach Kritikalität des Wasserstands priorisiert ab- oder zugeschaltet.

### Zustandsmaschine
```
         Wasserstand ok          Wasserstand unter SW1
NORMAL ─────────────────► WARNING ──────────────────► CRITICAL
  ▲         Wasserstand ok   │      Wasserstand ok        │
  └─────────────────────────┘◄───────────────────────────┘
```

### Zustände und Lastverteilung

**NORMAL** (Wasserstand > Schwellwert 1):
- Fan: läuft normal (klimagesteuert via UC4)
- LED: normal (helligkeitsgesteuert via UC4)
- Servo (Futterklappe): normal betriebsbereit
- Pumpe: PI-Regler aktiv (UC2)
- Display: Normalanzeige

**WARNING** (Wasserstand < Schwellwert 1):
- Fan: Mindestdrehzahl
- LED: gedimmt
- Servo: Futterklappe schließen (Ressourcen sparen)
- Pumpe: gestoppt (Wasserreserve schonen)
- Display: Warnung anzeigen
- Buzzer: kurzer Piepton alle 30s

**CRITICAL** (Wasserstand < Schwellwert 2):
- Fan: aus
- LED: aus
- Servo: geschlossen
- Pumpe: gesperrt
- Display: CRITICAL Meldung
- Buzzer: dauerhafter Alarm
- System fordert manuellen Eingriff

### Schwellwerte (konfigurierbar)
- SW1 (WARNING):  ADC-Wert 1500 (ca. 37% Füllstand)
- SW2 (CRITICAL): ADC-Wert 800  (ca. 20% Füllstand)

### Anforderungen
- State Machine im control_task auf esp_logic
- Zustandswechsel nur nach Hysterese (kein Flattern bei Grenzwerten)
- Jeder Zustandswechsel wird geloggt
- Aktueller Zustand auf LCD angezeigt

---

## UC4 – Klimaerfassung, Klimasteuerung & Alarmanlage

### Ziel
Automatische Klimasteuerung basierend auf Temperatur, Luftfeuchtigkeit und Helligkeit.
Zusätzlich eine zweizonen-Alarmanlage mit PIR und Ultraschall.

### UC4.1 – Klimasteuerung

**Temperaturregelung (Fan):**
- Wenn Temperatur > 28°C → Fan HIGH
- Wenn Temperatur > 25°C → Fan LOW
- Wenn Temperatur < 23°C → Fan OFF
- Hysterese von 1°C um Flattern zu vermeiden

**Helligkeitssteuerung (LED):**
- LDR io34: ADC-Wert < 1000 (dunkel) → LED an
- ADC-Wert > 2000 (hell) → LED aus
- Hysterese: 200 ADC-Punkte

**Feuchtigkeitsüberwachung (Steam Sensor):**
- Steam > Schwellwert → Fan HIGH (forciert, überschreibt Temperaturregel)
- Anzeige auf LCD

**Button (io5):**
- Kurz drücken (< 1s): Display-Modus wechseln (zeigt verschiedene Werte)
- Lang drücken (> 3s): Alarm manuell aktivieren/deaktivieren

### UC4.2 – Alarmanlage

**Zonen:**
- Zone A: PIR Sensor io23 (Seite Gebäude)
- Zone B: Ultrasonic Sensor io12/io13 (Rückseite, Objekt < 20cm = Einbruch)

**State Machine Alarm:**
```
DISARMED ──(Button lang)──► ARMED ──(Bewegung erkannt)──► TRIGGERED
   ▲                          │                               │
   └──────(Button lang)───────┘◄────────(Button lang)────────┘
```

**Modi (konfigurierbar über Button):**
- ALWAYS: Alarm immer aktiv wenn ARMED
- NIGHT_ONLY: Alarm nur wenn LDR-Wert < 1000 (dunkel)

**Bei Auslösung:**
- Buzzer: Dauerton
- LED: schnelles Blinken (200ms)
- Display: "ALARM ZONE A" oder "ALARM ZONE B"
- Logging: Zeitstempel + welche Zone

### Anforderungen
- Klimasteuerung und Alarmanlage laufen als separate State Machines im control_task
- Beide State Machines sind unabhängig voneinander
- Jeder Zustandswechsel wird via ESP_LOGI geloggt
- Hysterese bei allen Schwellwerten um Flattern zu vermeiden

---

## UC5 – IoT / WiFi Logging (Reserve)

### Ziel
WiFi-Anbindung des esp_logic für Daten-Logging und Remote-Monitoring.
Nur umsetzen wenn UC1–UC4 stabil laufen.

### Anforderungen (vorläufig)
- WiFi-Verbindung auf esp_logic (credentials in config.h, nicht im Code)
- Logging via MQTT oder einfachem HTTP-POST
- Folgende Daten loggen: Temperatur, Füllstand, Systemzustand, Alarm-Events
- Log-Intervall: 60 Sekunden (oder bei Zustandswechsel sofort)
- Bei WiFi-Ausfall: Normalbetrieb weiterführen (WiFi ist nicht kritisch)

---

## Repo-Struktur

```
/embedded_markdowns/
├── esp_hardware/          PlatformIO Projekt – Slave ESP32
│   ├── src/
│   │   ├── main.c
│   │   ├── sensor_task.c
│   │   ├── actuator_task.c
│   │   ├── comm_task.c
│   │   └── watchdog_task.c
│   ├── include/
│   │   ├── protocol.h
│   │   ├── sensor_task.h
│   │   ├── actuator_task.h
│   │   ├── comm_task.h
│   │   └── task_config.h
│   └── platformio.ini
├── esp_logic/             PlatformIO Projekt – Master ESP32
│   ├── src/
│   │   ├── main.c
│   │   ├── control_task.c
│   │   ├── comm_task.c
│   │   ├── display_task.c
│   │   ├── monitor_task.c
│   │   ├── watchdog_task.c
│   │   └── algorithms/
│   │       ├── pi_controller.c
│   │       └── state_machine.c
│   ├── include/
│   │   ├── protocol.h
│   │   ├── control_task.h
│   │   ├── algorithms/
│   │   │   ├── pi_controller.h
│   │   │   └── state_machine.h
│   │   └── task_config.h
│   ├── test/
│   │   └── test_pi_controller.c
│   └── platformio.ini
├── docs/
│   └── protocol/
│       └── uart_protocol.md
├── CLAUDE.md
└── README.md
```

---

## Reihenfolge der Umsetzung

```
UC1.1 → Shared Protocol Types (protocol.h)
UC1.2 → esp_hardware FreeRTOS Grundstruktur
UC1.3 → esp_logic FreeRTOS Grundstruktur
UC1.4 → UART2 Kommunikation (beide Seiten)
UC2   → PI-Regler Füllstand
UC3   → Lastmanagement State Machine
UC4.1 → Klimasteuerung
UC4.2 → Alarmanlage
UC5   → IoT/WiFi (optional)
```
