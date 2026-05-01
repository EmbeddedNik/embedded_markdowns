# Prompts for External Figure Generation

These prompts can be pasted into ChatGPT or another drawing assistant to create
polished thesis figures based on the Mermaid sources.

## System Architecture Figure

Create a clean technical block diagram for a bachelor thesis. Show a dual-ESP32
Smart Farm system with `esp_hardware` on the left and `esp_logic` on the right.
`esp_hardware` reads sensors and drives actuators. `esp_logic` runs control
logic, monitoring, serial profile input and a local WiFi/Web UI. Both controllers
communicate through UART2. Use a neutral academic style, white background,
German labels, and clear arrows for sensor data and actuator commands.

## FreeRTOS Task Figure

Create a FreeRTOS task overview for two ESP32 projects. On `esp_hardware`, show
`sensor_task`, `comm_task`, `actuator_task` and `watchdog_task`. On `esp_logic`,
show `comm_task`, `control_task`, `display_task`, `monitor_task`, `serial_task`,
`wifi_task` and `watchdog_task`. Use arrows for shared data, queues and UART2
communication. Keep the figure readable for a bachelor thesis.

## Control Algorithm Flowchart

Create a flowchart for the Smart Farm control cycle. Start with checking whether
fresh sensor data is available. If not, generate a safe actuator command. If yes,
evaluate water state, light state, fan behavior based on temperature and profile,
pump request from button, and night alarm from PIR. End with publishing the
actuator command. Use German labels and a clean engineering style.

## AI Development Workflow Figure

Create a comparison figure between two AI-assisted software development
workflows. The first workflow is GitHub issue based: issue, Claude mention,
GitHub action, feature branch, pull request. The second workflow is IDE based:
VS Code, Claude/Codex interaction, code inspection, local patch, hardware test,
iteration. Emphasize that human review and hardware validation are required in
both workflows.
