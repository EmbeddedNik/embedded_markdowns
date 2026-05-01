# UART2 Communication Protocol – esp_hardware ↔ esp_logic

## Overview

This document describes the binary protocol used for UART2 communication between
the two ESP32 nodes:

- **esp_hardware** (Slave) – hardware abstraction, sensor readout, actuator control
- **esp_logic** (Master) – control algorithms, state machines, decision logic

The protocol is the foundation of UC1 and carries all data exchanged between the
nodes: periodic sensor readings (UC2 water level, UC3 climate values) flow from
esp_hardware to esp_logic, while actuator commands (fan speed, pump state, servo
position, active profile) flow in the opposite direction.

Physical wiring (fixed, do not change):

| Signal        | Pin esp_hardware | Pin esp_logic |
|---------------|-----------------|---------------|
| TX → RX       | io2             | io32          |
| RX ← TX       | io4             | io33          |
| Common ground | GND             | GND           |

Baudrate: **115200**, 8N1

---

## Frame Format

Every message is wrapped in the following frame:

```
[ START_BYTE | MSG_TYPE | PAYLOAD_LEN | PAYLOAD | CHECKSUM ]
   1 byte       1 byte     1 byte       N bytes   1 byte
```

| Field         | Size       | Description                                                |
|---------------|------------|------------------------------------------------------------|
| `START_BYTE`  | 1 byte     | Always `0xAA` – marks the beginning of a frame            |
| `MSG_TYPE`    | 1 byte     | Message type identifier (see table below)                  |
| `PAYLOAD_LEN` | 1 byte     | Number of payload bytes that follow (0–32)                 |
| `PAYLOAD`     | 0–32 bytes | Message-specific data (see struct definitions below)       |
| `CHECKSUM`    | 1 byte     | CRC-8 over `MSG_TYPE`, `PAYLOAD_LEN`, and all payload bytes|

Maximum payload size: **32 bytes**.

Total frame struct size: **36 bytes** (`uart_frame_t`).

---

## Checksum Algorithm – CRC-8

The checksum is a CRC-8 with **polynomial `0x07`** and **initial value `0x00`**,
computed over `MSG_TYPE`, `PAYLOAD_LEN`, and every payload byte in order.
`START_BYTE` is **not** included.

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

The receiver recomputes the CRC over the received bytes and discards any frame where
the computed value does not match the transmitted `CHECKSUM` byte. The parser also
re-synchronises on the next `0xAA` byte after a checksum failure or any other
framing error.

---

## Message Types

| Constant           | Value  | Direction                       | Description                          |
|--------------------|--------|---------------------------------|--------------------------------------|
| `MSG_SENSOR_DATA`  | `0x01` | esp_hardware → esp_logic        | All sensor values, sent every 100 ms |
| `MSG_ACTUATOR_CMD` | `0x02` | esp_logic → esp_hardware        | Actuator set-points and active profile|
| `MSG_HEARTBEAT`    | `0x03` | esp_hardware → esp_logic        | Empty-payload keep-alive             |
| `MSG_ACK`          | `0x04` | both directions                 | Acknowledge a received frame         |

### Timing

- esp_hardware transmits `MSG_SENSOR_DATA` every **100 ms**.
- If esp_logic receives no message for **500 ms**, it must assume a communication
  fault and act accordingly.
- If esp_hardware receives no `MSG_ACTUATOR_CMD` for **500 ms**, it enters
  **FAILSAFE** state: pump off, fan off, servo neutral, buzzer off.

---

## Payload Structures

### MSG_SENSOR_DATA (`0x01`) – `sensor_data_t`

Payload size: **13 bytes** (packed, `__attribute__((packed))`, no padding).

All floating-point values are scaled to integers to avoid float over UART.

| Field            | Type       | Size   | Unit / Encoding                         | Range      | Pin     |
|------------------|------------|--------|-----------------------------------------|------------|---------|
| `water_level`    | `uint16_t` | 2 bytes| Raw ADC value                           | 0 – 4095   | io33    |
| `photoresistor`  | `uint16_t` | 2 bytes| Raw ADC value (0 = dark, 4095 = bright) | 0 – 4095   | io34    |
| `temperature`    | `int16_t`  | 2 bytes| °C × 10  (e.g. 235 = 23.5 °C)          | −400 – 800 | io17    |
| `humidity`       | `uint16_t` | 2 bytes| % × 10   (e.g. 654 = 65.4 %)           | 0 – 1000   | io17    |
| `ultrasonic_mm`  | `uint16_t` | 2 bytes| Distance in mm                          | 0 – 4000   | io12/13 |
| `pir_detected`   | `uint8_t`  | 1 byte | Motion detected: 0 = no, 1 = yes        | 0 or 1     | io23    |
| `button_pressed` | `uint8_t`  | 1 byte | Button state: 0 = released, 1 = pressed | 0 or 1     | io5     |
| `error_flags`    | `uint8_t`  | 1 byte | Bitmask (see below)                     | 0x00–0xFF  | —       |

**`error_flags` bitmask:**

| Bit | Mask   | Constant                  | Meaning                                          |
|-----|--------|---------------------------|--------------------------------------------------|
| 0   | `0x01` | `ERROR_FLAG_LDR`          | Photoresistor ADC read failed                    |
| 1   | `0x02` | `ERROR_FLAG_WATER_LEVEL`  | Water level ADC read failed                      |
| 2   | —      | —                         | Reserved, always 0                               |
| 3   | `0x08` | `ERROR_FLAG_DISTANCE`     | Ultrasonic sensor timeout                        |
| 4   | `0x10` | `ERROR_FLAG_TEMPERATURE`  | DHT temperature read failed                      |
| 5   | `0x20` | `ERROR_FLAG_HUMIDITY`     | DHT humidity read failed                         |
| 6   | —      | —                         | Reserved, always 0                               |
| 7   | `0x80` | `ERROR_FLAG_UART_TIMEOUT` | UART communication timeout detected on this node |

A set error flag means the corresponding field contains a stale or invalid value and
must not be used by the control layer.

---

### MSG_ACTUATOR_CMD (`0x02`) – `actuator_cmd_t`

Payload size: **7 bytes**.

| Field            | Type      | Size   | Description                                     | Range   | Pin     |
|------------------|-----------|--------|-------------------------------------------------|---------|---------|
| `pump_on`        | `uint8_t` | 1 byte | Water pump: 0 = off, 1 = on                     | 0 or 1  | io25    |
| `fan_speed`      | `uint8_t` | 1 byte | Fan PWM duty: 0 = off, >0 = on (io19 PWM)       | 0 – 255 | io18/19 |
| `servo_position` | `uint8_t` | 1 byte | Feed hatch: 0 = fully closed, 100 = fully open  | 0 – 100 | io26    |
| `led_on`         | `uint8_t` | 1 byte | Status LED: 0 = off, 1 = on                     | 0 or 1  | io27    |
| `buzzer_on`      | `uint8_t` | 1 byte | Buzzer: 0 = off, 1 = on                         | 0 or 1  | io16    |
| `display_mode`   | `uint8_t` | 1 byte | LCD display mode (see below)                    | 0 – 1   | I2C     |
| `profile`        | `uint8_t` | 1 byte | Active system profile (see below)               | 0 – 2   | —       |

**`display_mode` values:**

| Value | Meaning | System state                             |
|-------|---------|------------------------------------------|
| 0     | OK      | Water level sufficient, normal operation |
| 1     | REFILL  | Water level low, refill required         |

**`profile` values:**

| Value | Constant                  | Fan thresholds           | Use case                |
|-------|---------------------------|--------------------------|-------------------------|
| 0     | `SYSTEM_PROFILE_ECO`      | LOW ≥ 28 °C, HIGH ≥ 32 °C | Energy-saving mode    |
| 1     | `SYSTEM_PROFILE_NORMAL`   | LOW ≥ 25 °C, HIGH ≥ 28 °C | Standard operation    |
| 2     | `SYSTEM_PROFILE_PERFORMANCE` | LOW ≥ 23 °C, HIGH ≥ 26 °C | Maximum cooling    |

The active profile is selected by the operator via the serial console on esp_logic
and forwarded to esp_hardware in every `MSG_ACTUATOR_CMD` frame so that both nodes
share a consistent view of the current operating mode.

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

| Field         | Type          | Size     | Description                            |
|---------------|---------------|----------|----------------------------------------|
| `start_byte`  | `uint8_t`     | 1 byte   | Always `0xAA`                          |
| `msg_type`    | `uint8_t`     | 1 byte   | `MSG_*` constant                       |
| `payload_len` | `uint8_t`     | 1 byte   | Number of valid bytes in `payload`     |
| `payload`     | `uint8_t[32]` | 32 bytes | Message payload (packed struct cast)   |
| `checksum`    | `uint8_t`     | 1 byte   | CRC-8 (poly 0x07) checksum             |

Compile-time size assertions (`_Static_assert`) in `protocol.h` verify that
`sensor_data_t` (13 bytes), `actuator_cmd_t` (7 bytes), and `uart_frame_t` (36 bytes)
match their expected sizes and fit within `PROTO_MAX_PAYLOAD_LEN` (32 bytes).

---

## Safety Notes

- The **water pump** (`pump_on`) must never run continuously for more than 30 s.
  `actuator_task` on esp_hardware enforces a mandatory 5 s pause regardless of the
  command received.
- On UART timeout (no frame for 500 ms), esp_hardware sets all actuators to their
  safe state regardless of the last received command.
- Any sensor field accompanied by a set `error_flags` bit must not be forwarded to
  the control layer on esp_logic; `monitor_task` logs and reports stale data.
- The CRC-8 receiver rejects frames with a checksum mismatch and re-synchronises
  by scanning for the next `0xAA` start byte.
