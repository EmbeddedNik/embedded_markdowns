# CLAUDE.md – ESP32 Smart Farm

## Context
Bachelor thesis project. ESP32 embedded system using ESP-IDF + PlatformIO.
Two ESP32s: esp_hardware (slave, Smart Farm Kit) and esp_logic (master, logic & control).
UART2 communication between them (hardware TX=io2, RX=io4 | logic TX=io33, RX=io32).

## Rules
- Language: C (C99) only – no C++
- Framework: ESP-IDF only – no Arduino
- Use ESP-IDF drivers exclusively (driver/uart.h, driver/gpio.h, driver/i2c.h etc.)
- No malloc/free in control modules
- No global variables except in config files
- Integer arithmetic preferred, float only where physically necessary
- Watchdog Timer must be active on both ESP32s

## Conventions
- Filenames: snake_case
- Functions: module_prefix + snake_case → e.g. wifi_log_send(), mqtt_publish()
- Structs: snake_case + _t suffix → e.g. log_entry_t
- Constants: UPPER_SNAKE_CASE
- All comments in English
- Every file starts with: @file, @brief, @author, @date

## Project structure
- /esp_hardware/ – Slave ESP32 (Smart Farm Kit hardware)
- /esp_logic/    – Master ESP32 (logic, control, WiFi)
- /docs/         – documentation

## Workflow
- Code via Pull Request only – no direct push to main
- Commit messages: imperative English → "Add WiFi logging task"
