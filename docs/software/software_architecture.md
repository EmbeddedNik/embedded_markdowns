# Software Architecture Notes

This document collects implementation-oriented notes that can later be turned into
formal bachelor thesis documentation. It is intentionally more concrete than
`docs/requirements/REQUIREMENTS.md`.

## Project Context

The Smart Farm software is split into two PlatformIO/ESP-IDF projects:

| Project | Role | Main responsibility |
|---------|------|---------------------|
| `esp_hardware` | Smart Farm Kit controller | Reads sensors, drives actuators, enforces local actuator safety |
| `esp_logic` | Logic and control controller | Evaluates sensor data, runs state/control logic, provides Web UI |

The split keeps hardware-near code separated from decision logic. The two ESP32s
communicate through UART2 with a small framed protocol and CRC-8 checksum.

## Runtime Architecture

### esp_hardware

`esp_hardware` owns the physical Smart Farm Kit I/O.

| Task | Responsibility |
|------|----------------|
| `sensor_task` | Samples water level, LDR, DHT, ultrasonic sensor, PIR and button |
| `actuator_task` | Applies pump, fan, servo, LED, buzzer and LCD commands |
| `comm_task` | Sends sensor frames and receives actuator command frames via UART2 |
| `watchdog_task` | Configures and services task watchdog monitoring |

Important properties:

- Sensor data is stored in a mutex-protected shared structure.
- Actuator commands are transferred through a FreeRTOS queue.
- The pump has local safety limiting on the hardware controller.
- If actuator commands stop arriving, `esp_hardware` applies a failsafe state.
- Servo PWM is only attached during movement and is disabled afterward to reduce
  unwanted servo twitching.

### esp_logic

`esp_logic` owns the higher-level control and interface logic.

| Task | Responsibility |
|------|----------------|
| `comm_task` | Receives sensor data and sends actuator commands via UART2 |
| `control_task` | Runs water, light, fan and alarm logic |
| `display_task` | Maps system state into the outgoing display mode |
| `monitor_task` | Detects stale sensor data and logs safety-relevant warnings |
| `serial_task` | Allows profile selection through USB/UART0 |
| `wifi_task` | Starts SoftAP, HTTP server, dashboard and web API |
| `watchdog_task` | Configures and services task watchdog monitoring |

Important properties:

- Sensor snapshots are protected by a mutex.
- Actuator commands are produced cyclically and sent back to `esp_hardware`.
- The active profile can be changed through serial input or Web UI.
- The Web UI reads existing shared state; it does not replace the control loop.
- WiFi/HTTP startup failures are logged without stopping the remaining control tasks.

## Communication Flow

1. `esp_hardware` samples all sensors.
2. `esp_hardware` serializes current sensor data into a UART frame.
3. `esp_logic` parses the frame and updates the shared sensor snapshot.
4. `control_task` calculates actuator commands from the latest valid snapshot.
5. `esp_logic` sends the command frame back to `esp_hardware`.
6. `esp_hardware` maps the command to real actuators.

The UART protocol is deliberately small: start byte, message type, payload length,
payload and CRC-8. This makes the protocol easy to test and debug.

## Implemented Control Functions

### Water State

The water level is classified into `OK` or `REFILL`. Hysteresis is used to avoid
state chatter at the threshold. The state is forwarded to the display and Web UI.

### System Profiles

The active profile is one of:

- `ECO`
- `NORMAL`
- `PERFORMANCE`

The profile changes fan thresholds and is visible on the LCD/Web UI.

### Climate Behavior

The fan is driven from the current temperature and active profile. The LDR value
determines day/night behavior. The servo follows this light state and is only
powered by PWM while moving.

### Night Alarm

When the system is in night mode, PIR movement can trigger an alarm. A guard time
after the day/night transition suppresses false alarms caused by servo movement.

### Web UI

`esp_logic` creates the local WLAN `SmartFarm-ESP32` and serves a dashboard at:

```text
http://192.168.4.1/
```

The dashboard shows sensor values and state information and allows profile
switching through `POST /api/fan`.

## AI-Assisted Development Context

The project was developed with AI assistance in two modes:

- Claude Code GitHub action: issue-driven implementation through GitHub branches.
- VS Code extensions: interactive code inspection, debugging and integration work
  with Claude Code and Codex.

This is relevant for the thesis because several implementation steps were created
as issue-based AI contributions and later manually integrated, reviewed, tested
and corrected in the local development environment.

The PI controller requirement is handled in a separate project. This Smart Farm
implementation focuses on state-based control, communication, safety handling,
FreeRTOS integration and IoT visualization.
