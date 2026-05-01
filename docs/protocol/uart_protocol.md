# UART2 Kommunikationsprotokoll – esp_hardware ↔ esp_logic

## Übersicht

Dieses Dokument beschreibt das Binärprotokoll für die UART2-Kommunikation zwischen
den beiden ESP32-Knoten:

- **esp_hardware** (Slave) – Hardware-Abstraktion, Sensorauslese, Aktoransteuerung
- **esp_logic** (Master) – Steuerungsalgorithmen, Zustandsmaschinen, Entscheidungslogik

Das Protokoll bildet die Grundlage von UC1 und überträgt alle Daten zwischen den Knoten:
Periodische Sensorwerte (UC2 Wasserstand, UC3 Klimadaten) fließen von esp_hardware zu
esp_logic; Aktorbefehle (Lüftergeschwindigkeit, Pumpenstatus, Servo-Position, aktives Profil)
fließen in umgekehrter Richtung.

Physische Verkabelung (festgelegt, nicht ändern):

| Signal           | Pin esp_hardware | Pin esp_logic |
|------------------|-----------------|---------------|
| TX → RX          | io2             | io32          |
| RX ← TX          | io4             | io33          |
| Gemeinsame Masse | GND             | GND           |

Baudrate: **115200**, 8N1

---

## Frame-Format

Jede Nachricht ist in folgenden Frame eingebettet:

```
[ START_BYTE | MSG_TYPE | PAYLOAD_LEN | PAYLOAD | CHECKSUM ]
   1 Byte       1 Byte     1 Byte       N Bytes   1 Byte
```

| Feld          | Größe      | Beschreibung                                                      |
|---------------|------------|-------------------------------------------------------------------|
| `START_BYTE`  | 1 Byte     | Immer `0xAA` – markiert den Beginn eines Frames                   |
| `MSG_TYPE`    | 1 Byte     | Nachrichtentyp-Bezeichner (siehe Tabelle unten)                   |
| `PAYLOAD_LEN` | 1 Byte     | Anzahl der folgenden Payload-Bytes (0–32)                         |
| `PAYLOAD`     | 0–32 Bytes | Nachrichtenspezifische Daten (siehe Struct-Definitionen unten)    |
| `CHECKSUM`    | 1 Byte     | CRC-8 über `MSG_TYPE`, `PAYLOAD_LEN` und alle Payload-Bytes       |

Maximale Payload-Größe: **32 Bytes**.

Gesamtgröße der Frame-Struktur: **36 Bytes** (`uart_frame_t`).

---

## Prüfsummen-Algorithmus – CRC-8

Die Prüfsumme ist eine CRC-8 mit **Polynom `0x07`** und **Startwert `0x00`**,
berechnet über `MSG_TYPE`, `PAYLOAD_LEN` und jeden Payload-Byte in der angegebenen
Reihenfolge. Das `START_BYTE` ist **nicht** Bestandteil der Prüfsumme.

```c
static uint8_t crc8_byte(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    }
    return crc;
}

uint8_t calc_checksum(uint8_t msg_type, uint8_t payload_len, const uint8_t *payload)
{
    uint8_t crc = crc8_byte(0x00u, msg_type);
    crc = crc8_byte(crc, payload_len);
    for (uint8_t i = 0; i < payload_len; i++) {
        crc = crc8_byte(crc, payload[i]);
    }
    return crc;
}
```

Der Empfänger berechnet die CRC über die empfangenen Bytes neu und verwirft jeden Frame,
bei dem der berechnete Wert nicht mit dem übertragenen `CHECKSUM`-Byte übereinstimmt.
Nach einem Prüfsummenfehler oder einem sonstigen Frame-Fehler synchronisiert sich der
Parser am nächsten `0xAA`-Byte neu.

---

## Nachrichtentypen

| Konstante          | Wert   | Richtung                        | Beschreibung                                    |
|--------------------|--------|---------------------------------|-------------------------------------------------|
| `MSG_SENSOR_DATA`  | `0x01` | esp_hardware → esp_logic        | Alle Sensorwerte, alle 100 ms gesendet          |
| `MSG_ACTUATOR_CMD` | `0x02` | esp_logic → esp_hardware        | Aktorsollwerte und aktives Systemprofil         |
| `MSG_HEARTBEAT`    | `0x03` | esp_hardware → esp_logic        | Leere Keepalive-Nachricht                       |
| `MSG_ACK`          | `0x04` | beide Richtungen                | Empfangsbestätigung eines Frames                |

### Zeitverhalten

- esp_hardware sendet `MSG_SENSOR_DATA` alle **100 ms**.
- Empfängt esp_logic **500 ms** lang keine Nachricht, muss es einen Kommunikationsfehler
  annehmen und entsprechend reagieren.
- Empfängt esp_hardware **500 ms** lang keinen `MSG_ACTUATOR_CMD`, wechselt es in den
  **Failsafe**-Zustand: Pumpe aus, Lüfter aus, Servo neutral, Buzzer aus.

---

## Payload-Strukturen

### MSG_SENSOR_DATA (`0x01`) – `sensor_data_t`

Payload-Größe: **13 Bytes** (gepackt, `__attribute__((packed))`, kein Padding).

Alle Gleitkommawerte werden auf Ganzzahlen skaliert, um Float-Übertragung über UART zu vermeiden.

| Feld             | Typ        | Größe   | Einheit / Kodierung                          | Bereich    | Pin     |
|------------------|------------|---------|----------------------------------------------|------------|---------|
| `water_level`    | `uint16_t` | 2 Bytes | ADC-Rohwert                                  | 0 – 4095   | io33    |
| `photoresistor`  | `uint16_t` | 2 Bytes | ADC-Rohwert (0 = dunkel, 4095 = hell)        | 0 – 4095   | io34    |
| `temperature`    | `int16_t`  | 2 Bytes | °C × 10 (z. B. 235 = 23,5 °C)               | −400 – 800 | io17    |
| `humidity`       | `uint16_t` | 2 Bytes | % × 10 (z. B. 654 = 65,4 %)                 | 0 – 1000   | io17    |
| `ultrasonic_mm`  | `uint16_t` | 2 Bytes | Distanz in mm                                | 0 – 4000   | io12/13 |
| `pir_detected`   | `uint8_t`  | 1 Byte  | Bewegung erkannt: 0 = nein, 1 = ja           | 0 od. 1    | io23    |
| `button_pressed` | `uint8_t`  | 1 Byte  | Tasterstatus: 0 = nicht gedrückt, 1 = gedrückt | 0 od. 1 | io5     |
| `error_flags`    | `uint8_t`  | 1 Byte  | Bitmask (siehe unten)                        | 0x00–0xFF  | —       |

**`error_flags`-Bitmask:**

| Bit | Maske  | Konstante                 | Bedeutung                                              |
|-----|--------|---------------------------|--------------------------------------------------------|
| 0   | `0x01` | `ERROR_FLAG_LDR`          | Photoresistor ADC-Lesefehler                           |
| 1   | `0x02` | `ERROR_FLAG_WATER_LEVEL`  | Wasserstand ADC-Lesefehler                             |
| 2   | —      | —                         | Reserviert, immer 0                                    |
| 3   | `0x08` | `ERROR_FLAG_DISTANCE`     | Ultraschallsensor-Timeout                              |
| 4   | `0x10` | `ERROR_FLAG_TEMPERATURE`  | DHT Temperatur-Lesefehler                              |
| 5   | `0x20` | `ERROR_FLAG_HUMIDITY`     | DHT Luftfeuchte-Lesefehler                             |
| 6   | —      | —                         | Reserviert, immer 0                                    |
| 7   | `0x80` | `ERROR_FLAG_UART_TIMEOUT` | UART-Kommunikations-Timeout auf diesem Knoten          |

Ein gesetztes Fehlerflag bedeutet, dass das zugehörige Feld einen veralteten oder ungültigen
Wert enthält und von der Steuerungsschicht auf esp_logic nicht verwendet werden darf.

---

### MSG_ACTUATOR_CMD (`0x02`) – `actuator_cmd_t`

Payload-Größe: **7 Bytes**.

| Feld             | Typ       | Größe  | Beschreibung                                        | Bereich | Pin     |
|------------------|-----------|--------|-----------------------------------------------------|---------|---------|
| `pump_on`        | `uint8_t` | 1 Byte | Wasserpumpe: 0 = aus, 1 = an                        | 0 od. 1 | io25    |
| `fan_speed`      | `uint8_t` | 1 Byte | Lüfter PWM-Tastverhältnis: 0 = aus, >0 = an         | 0 – 255 | io18/19 |
| `servo_position` | `uint8_t` | 1 Byte | Klappe: 0 = vollständig geschlossen, 100 = offen    | 0 – 100 | io26    |
| `led_on`         | `uint8_t` | 1 Byte | Status-LED: 0 = aus, 1 = an                         | 0 od. 1 | io27    |
| `buzzer_on`      | `uint8_t` | 1 Byte | Buzzer: 0 = aus, 1 = an                             | 0 od. 1 | io16    |
| `display_mode`   | `uint8_t` | 1 Byte | LCD-Anzeigemodus (siehe unten)                      | 0 – 1   | I2C     |
| `profile`        | `uint8_t` | 1 Byte | Aktives Systemprofil (siehe unten)                  | 0 – 2   | —       |

**`display_mode`-Werte:**

| Wert | Bedeutung | Systemzustand                                  |
|------|-----------|------------------------------------------------|
| 0    | OK        | Wasserstand ausreichend, Normalbetrieb         |
| 1    | REFILL    | Wasserstand niedrig, Nachfüllen erforderlich   |

**`profile`-Werte:**

| Wert | Konstante                    | Lüfter-Schwellwerte         | Einsatzszenario      |
|------|------------------------------|-----------------------------|----------------------|
| 0    | `SYSTEM_PROFILE_ECO`         | LOW ≥ 28 °C, HIGH ≥ 32 °C  | Energiesparbetrieb   |
| 1    | `SYSTEM_PROFILE_NORMAL`      | LOW ≥ 25 °C, HIGH ≥ 28 °C  | Standardbetrieb      |
| 2    | `SYSTEM_PROFILE_PERFORMANCE` | LOW ≥ 23 °C, HIGH ≥ 26 °C  | Maximale Kühlung     |

Das aktive Profil wird vom Betreiber über die serielle Konsole auf esp_logic ausgewählt und
in jedem `MSG_ACTUATOR_CMD`-Frame an esp_hardware übertragen, sodass beide Knoten eine
einheitliche Sicht auf den aktuellen Betriebsmodus haben.

---

### MSG_HEARTBEAT (`0x03`)

Payload-Länge: **0 Bytes** (`PAYLOAD_LEN = 0x00`).  
Wird von esp_hardware alle 100 ms als Keepalive-Signal gesendet.

### MSG_ACK (`0x04`)

Payload-Länge: **0 Bytes** (`PAYLOAD_LEN = 0x00`).  
Wird von einem der beiden Knoten zur Bestätigung eines empfangenen Frames gesendet.

---

## Vollständige Frame-Struktur – `uart_frame_t`

Gesamtgröße der Struktur: **36 Bytes**.

| Feld          | Typ           | Größe    | Beschreibung                                   |
|---------------|---------------|----------|------------------------------------------------|
| `start_byte`  | `uint8_t`     | 1 Byte   | Immer `0xAA`                                   |
| `msg_type`    | `uint8_t`     | 1 Byte   | `MSG_*`-Konstante                              |
| `payload_len` | `uint8_t`     | 1 Byte   | Anzahl gültiger Bytes in `payload`             |
| `payload`     | `uint8_t[32]` | 32 Bytes | Nachrichtennutzdaten (gepackter Struct-Cast)   |
| `checksum`    | `uint8_t`     | 1 Byte   | CRC-8 (Polynom 0x07) Prüfsumme                 |

Kompilierzeit-Assertions (`_Static_assert`) in `protocol.h` stellen sicher, dass
`sensor_data_t` (13 Bytes), `actuator_cmd_t` (7 Bytes) und `uart_frame_t` (36 Bytes)
ihre erwarteten Größen einhalten und in `PROTO_MAX_PAYLOAD_LEN` (32 Bytes) passen.

---

## Sicherheitshinweise

- Die **Wasserpumpe** (`pump_on`) darf nie länger als 30 s kontinuierlich laufen.
  Der `actuator_task` auf esp_hardware erzwingt eine obligatorische Pause von 5 s,
  unabhängig vom empfangenen Befehl.
- Bei UART-Timeout (kein Frame für 500 ms) setzt esp_hardware alle Aktoren
  unabhängig vom letzten empfangenen Befehl in den sicheren Zustand.
- Jedes Sensor-Feld, zu dem ein `error_flags`-Bit gesetzt ist, darf nicht an die
  Steuerungsschicht auf esp_logic weitergeleitet werden; der `monitor_task`
  protokolliert und meldet veraltete Daten.
- Der CRC-8-Empfänger verwirft Frames mit nicht übereinstimmender Prüfsumme und
  synchronisiert sich neu, indem er nach dem nächsten `0xAA`-Start-Byte sucht.
