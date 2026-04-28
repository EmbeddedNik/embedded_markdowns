# UART2 Communication Protocol – esp_hardware ↔ esp_logic

## Overview

This document describes the binary protocol used for UART2 communication between
the two ESP32 nodes:

- **esp_hardware** (Slave) – hardware abstraction, sensor readout, actuator control
- **esp_logic** (Master) – control algorithms, state machines, decision logic

Physical wiring (fixed, do not change):

| Signal              | Pin esp_hardware | Pin esp_logic |
|---------------------|-----------------|---------------|
| TX → RX             | io2             | io32          |
| RX ← TX             | io4             | io33          |
| Common ground       | GND             | GND           |

Baudrate: **115200**, 8N1

---

## Frame Format

Every message is wrapped in the following frame:

```
[ START_BYTE | MSG_TYPE | PAYLOAD_LEN | PAYLOAD | CHECKSUM ]
   1 byte       1 byte     1 byte       N bytes   1 byte
```

| Field        | Size     | Description                                                   |
|--------------|----------|---------------------------------------------------------------|
| `START_BYTE` | 1 byte   | Always `0xAA` – marks the beginning of a frame               |
| `MSG_TYPE`   | 1 byte   | Message type identifier (see table below)                     |
| `PAYLOAD_LEN`| 1 byte   | Number of payload bytes that follow (0–32)                    |
| `PAYLOAD`    | 0–32 bytes | Message-specific data (see struct definitions below)        |
| `CHECKSUM`   | 1 byte   | XOR of `MSG_TYPE`, `PAYLOAD_LEN`, and all `PAYLOAD` bytes     |

Maximum payload size: **32 bytes**.

### Checksum Algorithm

The checksum is computed as the XOR of all bytes between `START_BYTE` and
`CHECKSUM` (exclusive):

```
checksum = MSG_TYPE ^ PAYLOAD_LEN ^ payload[0] ^ payload[1] ^ ... ^ payload[N-1]
```

`START_BYTE` is **not** included in the checksum.

---

## Message Types

| Constant           | Value  | Direction                       | Description                            |
|--------------------|--------|---------------------------------|----------------------------------------|
| `MSG_SENSOR_DATA`  | `0x01` | esp_hardware → esp_logic        | All sensor values, sent every 100 ms   |
| `MSG_ACTUATOR_CMD` | `0x02` | esp_logic → esp_hardware        | Actuator set-points                    |
| `MSG_HEARTBEAT`    | `0x03` | esp_hardware → esp_logic        | Empty-payload keep-alive               |
| `MSG_ACK`          | `0x04` | both directions                 | Acknowledge a received message         |

### Timing

- esp_hardware transmits `MSG_SENSOR_DATA` every **100 ms** (heartbeat).
- If esp_logic receives no message for **500 ms**, it must assume a communication
  fault and act accordingly.
- If esp_hardware receives no `MSG_ACTUATOR_CMD` for **500 ms**, it enters
  **FAILSAFE** state: pump off, fan off, servo neutral.

---

## Payload Structures

### MSG_SENSOR_DATA (`0x01`) – `sensor_data_t`

Payload size: **17 bytes** (packed, no padding).

All floating-point values are scaled to integers to avoid float over UART.

| Field            | Type       | Size   | Unit / Encoding                         | Range          | Pin     |
|------------------|------------|--------|-----------------------------------------|----------------|---------|
| `water_level`    | `uint16_t` | 2 bytes | Raw ADC value                          | 0 – 4095       | io33    |
| `soil_humidity`  | `uint16_t` | 2 bytes | Raw ADC value                          | 0 – 4095       | io32    |
| `photoresistor`  | `uint16_t` | 2 bytes | Raw ADC value (0 = dark, 4095 = bright) | 0 – 4095      | io34    |
| `steam_sensor`   | `uint16_t` | 2 bytes | Raw ADC value                          | 0 – 4095       | io35    |
| `temperature`    | `int16_t`  | 2 bytes | °C × 10  (e.g. 235 = 23.5 °C)         | −400 – 800     | io17    |
| `humidity`       | `uint16_t` | 2 bytes | % × 10   (e.g. 654 = 65.4 %)          | 0 – 1000       | io17    |
| `ultrasonic_mm`  | `uint16_t` | 2 bytes | Distance in mm                         | 0 – 4000       | io12/13 |
| `pir_detected`   | `uint8_t`  | 1 byte  | Motion detected: 0 = no, 1 = yes       | 0 or 1         | io23    |
| `button_pressed` | `uint8_t`  | 1 byte  | Button state: 0 = released, 1 = pressed| 0 or 1         | io5     |
| `error_flags`    | `uint8_t`  | 1 byte  | Bitmask (see below)                    | 0x00 – 0xFF    | —       |

**error_flags bitmask:**

| Bit | Mask   | Meaning                                              |
|-----|--------|------------------------------------------------------|
| 0   | `0x01` | `SENSOR_FAIL` – at least one sensor read failed      |
| 1   | `0x02` | `UART_TIMEOUT` – UART communication timeout detected |
| 2–7 | —      | Reserved, always 0                                   |

---

### MSG_ACTUATOR_CMD (`0x02`) – `actuator_cmd_t`

Payload size: **6 bytes**.

| Field            | Type      | Size   | Description                                    | Range       | Pin     |
|------------------|-----------|--------|------------------------------------------------|-------------|---------|
| `pump_on`        | `uint8_t` | 1 byte | Water pump: 0 = off, 1 = on                    | 0 or 1      | io25    |
| `fan_speed`      | `uint8_t` | 1 byte | Fan: 0 = off, 1 = low, 2 = high                | 0 – 2       | io18/19 |
| `servo_position` | `uint8_t` | 1 byte | Feed hatch: 0 = fully closed, 100 = fully open | 0 – 100     | io26    |
| `led_on`         | `uint8_t` | 1 byte | Status LED: 0 = off, 1 = on                    | 0 or 1      | io27    |
| `buzzer_on`      | `uint8_t` | 1 byte | Buzzer: 0 = off, 1 = on                        | 0 or 1      | io16    |
| `display_mode`   | `uint8_t` | 1 byte | LCD display mode (see below)                   | 0 – 3       | i2c     |

**display_mode values:**

| Value | Meaning  |
|-------|----------|
| 0     | Normal   |
| 1     | Warning  |
| 2     | Critical |
| 3     | Alarm    |

---

### MSG_HEARTBEAT (`0x03`)

Payload length: **0 bytes** (`PAYLOAD_LEN = 0x00`).
Sent by esp_hardware every 100 ms as a keep-alive signal.

### MSG_ACK (`0x04`)

Payload length: **0 bytes** (`PAYLOAD_LEN = 0x00`).
Sent by either node to acknowledge receipt of a frame.

---

## Full Frame Struct – `uart_frame_t`

Total struct size: **36 bytes**.

| Field         | Type          | Size    | Description                           |
|---------------|---------------|---------|---------------------------------------|
| `start_byte`  | `uint8_t`     | 1 byte  | Always `0xAA`                         |
| `msg_type`    | `uint8_t`     | 1 byte  | `MSG_*` constant                      |
| `payload_len` | `uint8_t`     | 1 byte  | Number of valid bytes in `payload`    |
| `payload`     | `uint8_t[32]` | 32 bytes| Message payload (packed struct cast)  |
| `checksum`    | `uint8_t`     | 1 byte  | XOR checksum (see algorithm above)    |

---

## Safety Notes

- The **water pump** (`pump_on`) must never run continuously for more than 30 s.
  The actuator_task on esp_hardware enforces a mandatory 5 s pause.
- On UART timeout (no message for 500 ms), esp_hardware sets all actuators to
  their safe state regardless of the last received command.
- All sensor values outside physically plausible ranges must be flagged via
  `error_flags` and not forwarded to the control layer.
