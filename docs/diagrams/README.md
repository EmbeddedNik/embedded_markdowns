# Diagrams

Architecture, communication, control and AI workflow diagrams for the ESP32 Smart Farm project.

---

## System Architecture

Overview of both ESP32s, their tasks and the UART2 communication link.

```mermaid
flowchart LR
    subgraph H["esp_hardware - Smart Farm Kit"]
        HS["Sensor task<br/>ADC, GPIO, DHT, ultrasonic"]
        HA["Actuator task<br/>pump, fan, servo, LED, buzzer, LCD"]
        HC["Communication task<br/>UART2 protocol"]
        HW["Watchdog task"]
    end

    subgraph L["esp_logic - Logic and Control"]
        LC["Communication task<br/>UART2 protocol"]
        CTRL["Control task<br/>state and control logic"]
        MON["Monitor task<br/>freshness and warnings"]
        SER["Serial task<br/>profile input"]
        WIFI["WiFi task<br/>SoftAP, HTTP, Web UI"]
        LW["Watchdog task"]
    end

    KIT["Smart Farm sensors"] --> HS
    HA --> ACT["Smart Farm actuators"]
    HS --> HC
    HC <-->|"UART2 frames"| LC
    LC --> CTRL
    CTRL --> LC
    CTRL --> MON
    SER --> CTRL
    WIFI --> CTRL
    WIFI --> UI["Browser dashboard"]
    HC --> HA
```

---

## FreeRTOS Task Overview

Task structure, inter-task data flow and watchdog monitoring on both ESP32s.

```mermaid
flowchart TB
    subgraph H["esp_hardware FreeRTOS tasks"]
        H1["sensor_task<br/>periodic sampling"]
        H2["comm_task<br/>UART TX/RX"]
        H3["actuator_task<br/>physical outputs"]
        H4["watchdog_task<br/>TWDT and stack monitoring"]
        H1 -->|"shared sensor data"| H2
        H2 -->|"actuator queue"| H3
        H4 -. monitors .-> H1
        H4 -. monitors .-> H2
        H4 -. monitors .-> H3
    end

    subgraph L["esp_logic FreeRTOS tasks"]
        L1["comm_task<br/>UART TX/RX"]
        L2["control_task<br/>control/state logic"]
        L3["display_task<br/>display mode"]
        L4["monitor_task<br/>stale data checks"]
        L5["serial_task<br/>profile input"]
        L6["wifi_task<br/>SoftAP/Web UI"]
        L7["watchdog_task<br/>TWDT and stack monitoring"]
        L1 -->|"sensor snapshot"| L2
        L2 -->|"actuator command"| L1
        L2 --> L3
        L4 --> L2
        L5 -->|"profile"| L2
        L6 -->|"read state / set profile"| L2
        L7 -. monitors .-> L1
        L7 -. monitors .-> L2
        L7 -. monitors .-> L3
        L7 -. monitors .-> L4
        L7 -. monitors .-> L5
        L7 -. monitors .-> L6
    end

    H2 <-->|"UART2"| L1
```

---

## UART Communication Sequence

Cyclic sensor/actuator frame exchange between both ESP32s including the failsafe path.

```mermaid
sequenceDiagram
    participant Sensors as Smart Farm sensors
    participant HW as esp_hardware
    participant UART as UART2
    participant Logic as esp_logic
    participant Act as Actuators

    loop cyclic operation
        HW->>Sensors: sample sensor values
        HW->>UART: send MSG_SENSOR_DATA
        UART->>Logic: receive and validate frame
        Logic->>Logic: update sensor snapshot
        Logic->>Logic: run state and control logic
        Logic->>UART: send MSG_ACTUATOR_CMD
        UART->>HW: receive and validate frame
        HW->>Act: apply actuator command
    end

    alt command timeout on esp_hardware
        HW->>Act: apply failsafe state
    end
```

---

## Web UI Sequence

Browser interaction with the SoftAP dashboard and the read/write API.

```mermaid
sequenceDiagram
    participant User as User device
    participant WiFi as SmartFarm-ESP32 WLAN
    participant Web as esp_logic wifi_task
    participant Ctrl as control_task state
    participant Comm as comm_task sensor snapshot

    User->>WiFi: connect to SoftAP
    User->>Web: GET /
    Web-->>User: HTML dashboard

    loop every dashboard refresh
        User->>Web: GET /api/data
        Web->>Comm: read latest sensor snapshot
        Web->>Ctrl: read water state and profile
        Web-->>User: JSON sensor and state data
    end

    opt user changes fan profile
        User->>Web: POST /api/fan
        Web->>Ctrl: update active profile
        Ctrl-->>Comm: next actuator command contains new profile
    end
```

---

## Water State Machine

Two-state hysteresis for water level classification.

```mermaid
stateDiagram-v2
    [*] --> OK
    OK --> REFILL: water level below refill threshold
    REFILL --> OK: water level above recovery threshold

    OK: water sufficient
    OK: display state OK
    REFILL: water too low
    REFILL: display state REFILL
```

---

## Light and Alarm State Machine

Day/night detection with PIR alarm behavior during night mode.

```mermaid
stateDiagram-v2
    [*] --> DAY
    DAY --> NIGHT: low LDR value confirmed
    NIGHT --> DAY: high LDR value confirmed

    DAY: servo in day position
    DAY: night alarm inactive
    NIGHT: servo in night position
    NIGHT: PIR alarm can be armed after guard time

    state NIGHT {
        [*] --> Guard
        Guard --> Armed: guard time elapsed
        Armed --> Alarm: PIR movement detected
        Alarm --> Armed: alarm hold time elapsed
    }
```

---

## Control Algorithm Flow

Main control cycle in `control_task`: sensor evaluation, state updates and actuator command generation.

```mermaid
flowchart TD
    A["Start control cycle"] --> B{"Fresh sensor data available?"}
    B -- no --> SAFE["Generate safe actuator command<br/>fan off, pump off, alarm off,<br/>day servo position, REFILL display"]
    B -- yes --> C["Copy latest sensor snapshot"]

    C --> W{"Water sensor valid?"}
    W -- yes --> W2["Update water state<br/>OK or REFILL with hysteresis"]
    W -- no --> W3["Keep previous water state"]

    W2 --> L
    W3 --> L
    L{"LDR valid?"} -- yes --> L2["Evaluate day/night candidate<br/>apply confirmation time"]
    L -- no --> L3["Keep previous light state"]

    L2 --> F
    L3 --> F
    F{"Temperature/humidity valid?"} -- yes --> F2["Calculate fan command<br/>using active profile thresholds"]
    F -- no --> F3["Fan off"]

    F2 --> A1["Set LED and servo from light state"]
    F3 --> A1
    A1 --> P["Set pump from button request"]
    P --> AL{"Night mode and PIR detected<br/>after guard time?"}
    AL -- yes --> AL2["Activate alarm hold<br/>buzzer on"]
    AL -- no --> AL3["Update alarm hold countdown"]
    AL2 --> OUT["Publish actuator command"]
    AL3 --> OUT
    SAFE --> OUT
```

---

## Sensor Validation Flow

Sensor sampling pipeline in `sensor_task` including rate-of-change checks and plausibility guards.

```mermaid
flowchart TD
    A["sensor_task cycle"] --> ADC["Read ADC sensors<br/>water level and LDR"]
    ADC --> ROC["Check implausible jumps<br/>rate-of-change detection"]
    ROC --> D["Read digital inputs<br/>PIR and button"]
    D --> US["Measure ultrasonic distance<br/>with timeout"]
    US --> DHT{"DHT read interval reached?"}
    DHT -- yes --> DHTR["Read DHT<br/>retry once on timing failure"]
    DHT -- no --> OLD["Reuse previous DHT values"]
    DHTR --> P["Apply plausibility checks<br/>and preserve last good values"]
    OLD --> P
    P --> M["Publish protected sensor snapshot"]
    M --> LOG{"Log interval or error transition?"}
    LOG -- yes --> L["Write serial diagnostic log"]
    LOG -- no --> END["Wait until next cycle"]
    L --> END
```

---

## Actuator Safety Flow

Actuator execution in `actuator_task` including servo hold logic and pump safety limiting.

```mermaid
flowchart TD
    A["actuator_task cycle"] --> Q{"New command in queue?"}
    Q -- yes --> C["Use latest command"]
    Q -- no --> K["Keep previous command"]

    C --> FAN
    K --> FAN
    FAN["Apply fan command"] --> SERVO{"Servo command enabled?"}
    SERVO -- yes --> ST{"Target changed?"}
    ST -- yes --> SPWM["Attach PWM and move servo"]
    ST -- no --> SH{"Hold time elapsed?"}
    SH -- yes --> SD["Stop PWM and hold signal low"]
    SH -- no --> PUMP
    SPWM --> PUMP
    SD --> PUMP
    SERVO -- no --> SSAFE["Disable servo PWM<br/>and forget target"]
    SSAFE --> PUMP

    PUMP{"Pump requested?"} -- yes --> PLIM{"Max on-time reached?"}
    PLIM -- yes --> POFF["Force pump off<br/>enter cooldown"]
    PLIM -- no --> PON["Pump on"]
    PUMP -- no --> POFF2["Pump off"]
    POFF --> BUZ
    PON --> BUZ
    POFF2 --> BUZ

    BUZ["Apply buzzer and LED"] --> LCD["Refresh LCD periodically"]
    LCD --> END["Wait until next cycle"]
```

---

## Fault Handling Flow

Runtime fault sources and the corresponding safe reactions in each ESP32.

```mermaid
flowchart TD
    A["Runtime fault source"] --> B{"Fault type"}
    B -->|invalid sensor value| S["Set sensor error flag<br/>use safe/last valid value"]
    B -->|missing actuator commands| F["esp_hardware failsafe<br/>actuators off"]
    B -->|stale sensor data| L["esp_logic safe command<br/>fan/pump/alarm off"]
    B -->|WiFi startup error| W["Log error<br/>continue control tasks"]
    B -->|task stack low| T["Watchdog task logs warning"]

    S --> O["Visible in serial logs<br/>and transmitted status"]
    F --> O
    L --> O
    W --> O
    T --> O
```

---

## Logging and Observability

Observability mechanisms and how they feed into thesis evaluation.

```mermaid
flowchart LR
    SYS["Embedded runtime"] --> LOG["Serial logs"]
    SYS --> WEB["Web UI"]
    SYS --> LCD["LCD display"]
    SYS --> TEST["Host-side unit tests"]

    LOG --> D1["Debug timing, communication,<br/>state changes and faults"]
    WEB --> D2["Observe current sensor values<br/>and profile state"]
    LCD --> D3["Local state feedback<br/>profile and water state"]
    TEST --> D4["Verify pure protocol and<br/>control logic modules"]

    D1 --> THESIS["Evidence for evaluation<br/>and discussion"]
    D2 --> THESIS
    D3 --> THESIS
    D4 --> THESIS
```

---

## AI Development Workflow

End-to-end flow from GitHub issue via Claude Code action to validated main branch.

```mermaid
flowchart LR
    Issue["GitHub issue<br/>use case task"] --> Mention["@claude in issue"]
    Mention --> Action["Claude Code GitHub Action"]
    Action --> Branch["AI-created feature branch"]
    Branch --> PR["Pull request / branch review"]
    PR --> Local["Local VS Code integration<br/>Claude Code + Codex"]
    Local --> Test["Build, flash, hardware test"]
    Test --> Fix["Manual corrections and integration"]
    Fix --> Main["Merge into main after validation"]

    CLAUDE["CLAUDE.md<br/>agent rules and project context"] --> Action
    CLAUDE --> Local
```

---

## AI Workflow Comparison

GitHub agent workflow versus IDE assistant workflow side by side.

```mermaid
flowchart TB
    subgraph G["GitHub agent workflow"]
        G1["Create issue"] --> G2["Mention Claude"]
        G2 --> G3["Claude Code Action runs"]
        G3 --> G4["Feature branch"]
        G4 --> G5["PR / merge conflicts possible"]
    end

    subgraph I["IDE assistant workflow"]
        I1["Open code in VS Code"] --> I2["Ask Claude/Codex"]
        I2 --> I3["Inspect code and run tools"]
        I3 --> I4["Patch locally"]
        I4 --> I5["Flash and test hardware"]
        I5 --> I2
    end

    G5 --> R["Human review and integration"]
    I5 --> R
    R --> M["Validated software state"]
```

---

## Thesis Methodology Flow

Design-build-test methodology used throughout the project.

```mermaid
flowchart TD
    A["Define use cases and constraints"] --> B["Design modular architecture"]
    B --> C["Implement ESP-IDF/FreeRTOS modules"]
    C --> D["Use AI assistance<br/>GitHub agent and IDE pair-programming"]
    D --> E["Build and flash both ESP32 projects"]
    E --> F["Validate on real hardware"]
    F --> G["Observe behavior through logs, LCD and Web UI"]
    G --> H["Refine implementation"]
    H --> I["Document architecture, behavior and AI workflow"]
    H --> C
```
