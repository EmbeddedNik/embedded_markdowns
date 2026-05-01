# Module Catalog

This catalog describes the main source files by responsibility. It is meant for
documentation and thesis writing, not as API reference.

## esp_hardware

`esp_hardware` is the hardware abstraction and actuator execution layer.

| File | Responsibility | Notes for documentation |
|------|----------------|-------------------------|
| `src/main.c` | Initializes subsystems and starts FreeRTOS tasks | Good entry point for task architecture diagram |
| `src/sensor_task.c` | Reads all Smart Farm Kit sensors cyclically | Contains ADC, DHT, ultrasonic, PIR and button handling |
| `src/actuator_task.c` | Applies actuator commands to pump, fan, servo, LED, buzzer and LCD | Contains pump safety and servo PWM handling |
| `src/comm_task.c` | UART2 communication with esp_logic | Sends sensor data and receives actuator commands |
| `src/lcd.c` | HD44780/I2C LCD driver helper | Supports display of profile and water state |
| `src/roc_detector.c` | Rate-of-change plausibility helper | Used to detect implausible ADC jumps |
| `src/watchdog_task.c` | Task watchdog and stack high-water monitoring | Supports runtime robustness and observability |

### esp_hardware data flow

1. `sensor_task` samples physical inputs.
2. Sensor values are stored in a shared protected structure.
3. `comm_task` serializes and transmits these values to `esp_logic`.
4. `comm_task` receives actuator commands from `esp_logic`.
5. Commands are pushed into the actuator queue.
6. `actuator_task` maps commands to physical outputs.

## esp_logic

`esp_logic` is the decision-making, monitoring and user-interface layer.

| File | Responsibility | Notes for documentation |
|------|----------------|-------------------------|
| `src/main.c` | Initializes subsystems and starts FreeRTOS tasks | Good entry point for task architecture diagram |
| `src/comm_task.c` | UART2 communication with esp_hardware | Maintains latest sensor snapshot and outgoing actuator command |
| `src/control_task.c` | Main control and state logic | Water state, light state, fan profile behavior and night alarm |
| `src/control_logic.c` | Pure helper logic for control decisions | Suitable for unit testing and algorithm explanation |
| `src/display_task.c` | Maps system state to outgoing display mode | Small bridge between logic and hardware display |
| `src/monitor_task.c` | Checks stale data and logs warnings | Supports robustness and diagnosis |
| `src/serial_task.c` | Allows profile changes via USB serial input | Local operator interface |
| `src/wifi_task.c` | SoftAP, HTTP server, dashboard and Web API | Local IoT interface for monitoring and profile switching |
| `src/proto_utils.c` | CRC-8 and UART parser utilities | Host-tested protocol logic |
| `src/watchdog_task.c` | Task watchdog and stack high-water monitoring | Runtime robustness and debugging aid |

### esp_logic data flow

1. `comm_task` receives a valid sensor frame from `esp_hardware`.
2. `control_task` reads the latest valid sensor snapshot.
3. Control and state logic calculate the next actuator command.
4. `comm_task` sends that command back to `esp_hardware`.
5. `serial_task` and `wifi_task` can update the active profile.
6. `wifi_task` also exposes the current sensor/state snapshot to the Web UI.

## Documentation-relevant design decisions

### Separation into two controllers

The split between `esp_hardware` and `esp_logic` makes the software easier to
explain:

- hardware acquisition and output execution are isolated,
- algorithmic decisions are centralized,
- the UART protocol is the explicit interface between both worlds.

### FreeRTOS task model

The task model maps naturally to thesis diagrams:

- cyclic sensor acquisition,
- cyclic communication,
- cyclic control decision,
- asynchronous operator input,
- web server interaction,
- watchdog supervision.

### Observability

The system deliberately uses serial logs and the Web UI to make internal state
visible during tests. This is important because embedded systems often fail in
ways that cannot be understood from source code alone.
