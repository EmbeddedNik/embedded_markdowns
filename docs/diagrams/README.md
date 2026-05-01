# Diagram Sources

This directory contains Mermaid diagram sources for thesis documentation and
figures. They can be previewed in VS Code with a Mermaid Markdown extension or
converted into SVG/PNG for the written thesis.

Suggested figures:

| File | Purpose |
|------|---------|
| `system_architecture.mmd` | Overall two-ESP32 software architecture |
| `uart_sequence.mmd` | Runtime communication between both ESP32s |
| `freertos_task_overview.mmd` | Task structure and shared data flow |
| `water_state_machine.mmd` | Water OK/REFILL state machine |
| `light_alarm_state_machine.mmd` | Day/night and night alarm behavior |
| `webui_sequence.mmd` | Browser/Web UI interaction |
| `ai_development_workflow.mmd` | AI-assisted development workflow |
| `control_algorithm_flow.mmd` | Main control-cycle flowchart |
| `sensor_validation_flow.mmd` | Sensor sampling and plausibility flow |
| `actuator_safety_flow.mmd` | Actuator execution and safety flow |
| `fault_handling_flow.mmd` | Fault sources and safe reactions |
| `logging_observability_flow.mmd` | Logs, Web UI, LCD and tests as evidence sources |
| `ai_workflow_comparison.mmd` | GitHub agent workflow vs. IDE assistant workflow |
| `thesis_methodology_flow.mmd` | Thesis method/design-build-test workflow |
| `prompts_for_figures.md` | Text prompts for polished external figures |

If a rendered figure should look more polished than Mermaid output, these files
can be used as input/context for a drawing or image generation tool.
