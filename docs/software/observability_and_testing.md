# Observability and Testing Notes

The Smart Farm implementation was validated through a combination of code-level
tests, serial logging, Web UI observation and real hardware behavior.

## Observability Mechanisms

### Serial logs

Both ESP32 projects use serial logging to expose runtime behavior. Relevant log
categories include:

| Module | What is visible in logs |
|--------|-------------------------|
| `sensor_task` | sensor values, sensor error transitions |
| `comm_task` | UART frame errors, link loss and link recovery |
| `control_task` | active profile, water state, light state, actuator command summary |
| `actuator_task` | servo movement, pump safety events, LCD recovery events |
| `wifi_task` | SoftAP startup, HTTP server startup, profile changes through Web UI |
| `watchdog_task` | low stack high-water warnings |

These logs were essential for diagnosing integration issues such as missing
hardware connections, UART frame errors, WiFi startup failures and servo
startup behavior.

### Web UI

The Web UI provides a high-level runtime view:

- temperature,
- humidity,
- water level,
- LDR value,
- PIR state,
- data validity,
- water state,
- active fan profile.

It is useful as a demonstrator output because it shows that embedded state is
observable without a serial terminal.

### Host-side tests

The repository contains host-side tests for pure C logic:

- UART protocol checksum and parser behavior,
- water/light state transitions,
- fan-speed calculation.

This validates the parts of the system that can be separated from ESP-IDF and
hardware timing.

## Hardware Validation

Hardware validation remains necessary for:

- DHT timing behavior,
- ultrasonic sensor timeouts,
- ADC noise and plausibility behavior,
- UART wiring and common ground,
- servo behavior during boot and reset,
- WiFi SoftAP availability,
- end-to-end Web UI profile switching.

This distinction is important for the thesis: not every embedded behavior can be
verified by unit tests alone.

## Example Test and Debug Scenarios

| Scenario | Expected observation |
|----------|----------------------|
| Both ESP32s connected and running | UART link stable, no repeated frame errors |
| esp_hardware disconnected | esp_logic logs stale data/link warnings |
| esp_logic disconnected | esp_hardware enters failsafe |
| Water level below threshold | system state changes to REFILL |
| LDR changes persistently | day/night state changes after confirmation delay |
| PIR movement in night mode | alarm path activates after guard time |
| Web UI profile button pressed | active profile changes and fan behavior follows profile |
| WiFi startup failure | control tasks continue running and error is logged |

## Lessons Learned

Several issues only became visible on real hardware:

- WiFi required robust NVS initialization.
- Opening a serial console can reset an ESP32 dev board through USB-UART control
  lines.
- Servo signal handling required care during boot and after PWM movement.
- Missing physical connection between controllers can look like software parser
  failure until the hardware setup is checked.

These observations are useful discussion material because they show the gap
between generated code, successful compilation and real embedded behavior.
