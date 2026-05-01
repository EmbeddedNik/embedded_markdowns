# Anforderungen – Bachelor ESP32 Smart Farm

## Projektübersicht

Bachelorarbeit an der Hochschule Burgenland, Studiengang Angewandte Elektronik und Photonik.  
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
│  - Keine Regelungslogik     │         │  - Sicherheitsüberwachung   │
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

### Sicherheitsregeln (verbindlich überall)
- **Wasserpumpe:** max. 30 s kontinuierlich eingeschaltet, danach min. 5 s Pause (im `actuator_task` erzwingen)
- **Alle Sensoren:** Plausibilitätsprüfung; bei ungültigem Wert Fehlerflag setzen
- **UART Timeout:** kein Frame in 500 ms → esp_hardware in FAILSAFE (alle Aktoren aus)
- **Watchdog Timer** auf beiden ESP32s aktiv (TWDT, Timeout 5000 ms)
- Bei Fehler: Fehlerflag + LCD-Ausgabe + Logging (nie still ignorieren)

---

## UC1 - Kommunikationsschicht & Systemgrundstruktur

### Ziel
UC1 legt die gemeinsame technische Basis fuer beide ESP32s. Das System wird in zwei klar getrennte Rollen aufgeteilt: esp_hardware liest Sensoren und steuert Aktoren, esp_logic verarbeitet die Sensordaten und trifft Steuerentscheidungen.

---

### UC1.1 - Kommunikation zwischen esp_hardware und esp_logic

Die beiden ESP32s kommunizieren ueber UART2. Die Verbindung muss Sensordaten, Fehlerzustaende und Aktorbefehle zuverlaessig uebertragen und fehlerhafte Nachrichten erkennen.

**Anforderungen:**
- esp_hardware sendet zyklisch Sensordaten an esp_logic
- esp_logic sendet Aktorbefehle an esp_hardware
- Nachrichten enthalten eine einfache Fehlererkennung
- ungueltige oder unvollstaendige Nachrichten duerfen keine ungueltigen Aktorbefehle ausloesen
- bei Kommunikationsausfall muss esp_hardware einen sicheren Zustand einnehmen

**Uebertragene Informationen:**
| Richtung | Inhalt |
|----------|--------|
| esp_hardware -> esp_logic | Wasserstand, Helligkeit, Temperatur, Luftfeuchte, Distanz, PIR, Button, Fehlerstatus |
| esp_logic -> esp_hardware | Pumpe, Luefter, Servo, LED, Buzzer, Displaystatus, aktives Profil |

---

### UC1.2 - Grundstruktur esp_hardware

esp_hardware uebernimmt die hardwarenahe Ebene des Systems. Dort werden Sensorwerte erfasst, Aktoren angesteuert und Sicherheitsgrenzen fuer physische Ausgaenge eingehalten.

**Anforderungen:**
- zyklisches Einlesen aller Sensoren des Smart-Farm-Kits
- Plausibilitaetspruefung der Messwerte und Setzen von Fehlerflags
- Ausfuehren der von esp_logic empfangenen Aktorbefehle
- Erzwingen lokaler Sicherheitsregeln, insbesondere fuer die Wasserpumpe
- sicherer Zustand bei ausbleibenden Befehlen von esp_logic
- Watchdog-Ueberwachung fuer die laufenden Tasks

---

### UC1.3 - Grundstruktur esp_logic

esp_logic uebernimmt die zentrale Logik- und Steuerungsebene. Dort werden Sensordaten bewertet, Zustaende berechnet und Aktorbefehle fuer esp_hardware erzeugt.

**Anforderungen:**
- Empfang und Zwischenspeicherung aktueller Sensordaten
- zyklische Ausfuehrung der Steuerungs- und Zustandslogik
- Erzeugung konsistenter Aktorbefehle fuer esp_hardware
- Bedienmoeglichkeit fuer das aktive Systemprofil
- Ueberwachung der Datenaktualitaet und sicherheitsrelevanter Zustaende
- Watchdog-Ueberwachung fuer die laufenden Tasks

---

## UC2 - Wasserstandsueberwachung & profilbasierte Systemsteuerung

### Ziel
UC2 ueberwacht den Wasserstand und stellt ein dreistufiges Profilmodell bereit. Das System soll dem Betreiber einen niedrigen Fuellstand anzeigen und die Lueftersteuerung an unterschiedliche Betriebsanforderungen anpassen koennen.

---

### UC2.1 - Wasserstandszustand

Der Wasserstand wird kontinuierlich bewertet. Bei zu niedrigem Fuellstand wechselt das System in einen REFILL-Zustand, bis wieder ausreichend Wasser erkannt wird. Eine Hysterese verhindert haeufiges Umschalten an der Grenze.

**Zustaende:**
| Zustand | Bedeutung |
|---------|-----------|
| OK | Wasserstand ausreichend |
| REFILL | Wasserstand zu niedrig, Nachfuellen erforderlich |

**Anforderungen:**
- niedriger Wasserstand muss erkannt und angezeigt werden
- Zustandswechsel muessen gegen Messwertflattern abgesichert sein
- der aktuelle Wasserzustand muss an esp_hardware uebertragen und am Display/Webinterface darstellbar sein
- Sensorfehler duerfen nicht still ignoriert werden

---

### UC2.2 - Systemprofile

Das System unterstuetzt drei Profile fuer unterschiedliche Betriebsarten. Das aktive Profil beeinflusst insbesondere die Lueftersteuerung und kann durch den Betreiber geaendert werden.

**Profile:**
| Profil | Bedeutung |
|--------|-----------|
| ECO | energiesparender Betrieb |
| NORMAL | Standardbetrieb |
| PERFORMANCE | staerkere Klimatisierung |

**Anforderungen:**
- Profilwechsel zwischen ECO, NORMAL und PERFORMANCE
- Profil bleibt zentrale Eingangsvariable fuer die Lueftersteuerung
- aktives Profil wird an esp_hardware uebertragen und angezeigt
- Profilwechsel muss ohne Neustart des Systems moeglich sein

---

## UC3 - Klimasteuerung & Alarmanlage

### Ziel
UC3 automatisiert einfache Klima- und Sicherheitsfunktionen. Temperatur und Helligkeit beeinflussen Luefter, LED und Servo. Der PIR-Sensor dient zur Bewegungserkennung im Nachtbetrieb.

---

### UC3.1 - Klimasteuerung

Die Klimasteuerung nutzt Temperatur, Helligkeit und das aktive Profil, um Luefter und Tag/Nacht-Verhalten zu bestimmen.

**Anforderungen:**
- Luefter reagiert temperaturabhaengig und profilabhaengig
- Helligkeit bestimmt den Tag/Nacht-Zustand
- der Servo folgt dem Tag/Nacht-Zustand der Anlage
- Schwellwerte muessen mit Hysterese oder Bestaetigungszeit gegen Flattern abgesichert sein
- Sensorfehler fuehren zu einem sicheren Verhalten der betroffenen Aktoren

---

### UC3.2 - Nacht-Alarmanlage

Im Nachtbetrieb soll das System Bewegungen erkennen und den Betreiber akustisch/optisch warnen. Fehlalarme durch den Uebergang in den Nachtzustand sollen vermieden werden.

**Anforderungen:**
- PIR-Bewegung im Nachtzustand loest Alarm aus
- nach dem Wechsel in den Nachtzustand gilt eine kurze Schutzzeit
- Alarm wird ueber Buzzer und LED signalisiert
- Alarmzustand wird in der Systemanzeige beruecksichtigt
- Alarm- und Klimafunktionen duerfen die UART-Kommunikation nicht blockieren

---

## UC4 - IoT-Webinterface & lokale WLAN-Bedienung

### Ziel
Der esp_logic stellt ein lokales WLAN bereit, ueber das sich ein Betreiber direkt mit dem
Smart-Farm-System verbinden kann. Ueber ein Webinterface koennen aktuelle Sensorwerte und
Systemzustaende eingesehen sowie das aktive Luefterprofil geaendert werden. Die bestehende
FreeRTOS- und UART2-Kommunikationsstruktur bleibt dabei erhalten.

---

### UC4.1 - Lokales WLAN und HTTP-Server

Der esp_logic arbeitet als WLAN Access Point. Ein Bediengeraet kann sich direkt mit diesem
Netzwerk verbinden und die Weboberflaeche ueber die lokale IP-Adresse des ESP32 aufrufen.

**Anforderungen:**
- esp_logic erzeugt ein eigenes WLAN fuer den lokalen Zugriff
- HTTP-Server laeuft als eigene FreeRTOS-Task
- Bestehende Tasks fuer Kommunikation, Steuerung und Sicherheit duerfen nicht blockiert werden
- Bei WLAN- oder HTTP-Fehlern muss das restliche System weiterlaufen

---

### UC4.2 - Anzeige aktueller Messwerte

Das Webinterface zeigt die vom esp_hardware empfangenen Sensordaten an. Die Daten stammen aus
der bestehenden UART2-Kommunikation und werden im esp_logic nur visualisiert.

**Anzuzeigende Werte:**
| Wert            | Quelle                         |
|-----------------|--------------------------------|
| Temperatur      | `sensor_data_t.temperature`    |
| Luftfeuchte     | `sensor_data_t.humidity`       |
| Wasserstand     | `sensor_data_t.water_level`    |
| Helligkeit/LDR  | `sensor_data_t.photoresistor`  |
| PIR-Zustand     | `sensor_data_t.pir_detected`   |

---

### UC4.3 - Anzeige von Systemzustaenden

Neben den Messwerten zeigt das Webinterface wichtige Systemzustaende an, damit der Betreiber den
aktuellen Anlagenzustand ohne serielle Konsole beurteilen kann.

**Anzuzeigende Zustaende:**
| Zustand             | Bedeutung                          |
|---------------------|------------------------------------|
| Wasserzustand       | OK / REFILL                        |
| Aktives Profil      | ECO / NORMAL / PERFORMANCE         |
| Datenstatus         | gueltige oder fehlende Sensordaten |

---

### UC4.4 - Profilumschaltung ueber Webinterface

Das aktive Systemprofil kann ueber das Webinterface geaendert werden. Die Umschaltung wirkt auf
dieselbe globale Profilvariable wie die serielle Bedienung und beeinflusst damit die bestehende
profilabhaengige Lueftersteuerung aus UC2/UC3.

**Anforderungen:**
- Auswahl zwischen ECO, NORMAL und PERFORMANCE
- Profilwechsel wird im esp_logic uebernommen und zyklisch an esp_hardware uebertragen
- Serielle Profilumschaltung bleibt weiterhin moeglich
- Ungueltige Web-Anfragen duerfen keinen undefinierten Zustand erzeugen

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
UC4.1 -> Lokales WLAN und HTTP-Server auf esp_logic
UC4.2 -> Anzeige aktueller Sensordaten im Webinterface
UC4.3 -> Anzeige zentraler Systemzustaende im Webinterface
UC4.4 -> Profilumschaltung ueber Webinterface
Tests → Unity Unit-Tests für protocol und control_logic (pio test -e native)
```
