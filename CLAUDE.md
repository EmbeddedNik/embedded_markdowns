# CLAUDE.md – Bachelor ESP32 Algorithms

## Projektkontext
Bachelorarbeit an der Hochschule Burgenland, Studiengang Angewandte Elektronik
und Photonik.

Ziel: Modulare Implementierung und Verifikation eingebetteter Steuerungs-
und Regelungsalgorithmen auf einer ESP32-Mikrocontrollerplattform.
Zusätzliche Forschungsfrage: Eignung von KI-gestützter Entwicklung
(Vibe Coding) für eingebettete Systeme.

## Plattform & Toolchain
- Hardware: ESP32 (Dual-Core Xtensa LX6, 240MHz)
- Framework: ESP-IDF (espressif/arduino-esp32 NICHT verwenden)
- Build-System: PlatformIO in VS Code
- Sprache: C (C99) – kein C++
- RTOS: FreeRTOS (bereits in ESP-IDF enthalten)

## Wichtig zu Hardware-Treibern
- Für I2C, SPI, UART, GPIO ausschließlich die ESP-IDF eigenen
  Treiber verwenden (driver/i2c.h, driver/spi_master.h etc.)
- Keine eigenen Low-Level Treiber implementieren
- HAL-Layer von ESP-IDF nutzen wo vorhanden

## Architektur-Prinzipien
- Strikte Trennung in drei Schichten:
  1. HAL (Hardware Abstraction Layer) – nur IDF-Treiber, kein Algorithmus-Code
  2. Algorithm Layer – reine Regelungslogik, kein direkter Hardware-Zugriff
  3. Application Layer – verbindet HAL und Algorithmen, FreeRTOS Tasks
- Algorithmen sind hardware-unabhängig und über Interfaces testbar
- Kein malloc/free in Regelungsmodulen (statische Speicherverwaltung)
- Integer-Arithmetik bevorzugen – float nur wenn fachlich begründet
- Keine globalen Variablen außer in klar gekennzeichneten Konfigurationsdateien

## Algorithmen im Scope
- Diskreter PI-Regler (Tustin/Bilinear-Diskretisierung)
- Finite State Machines (Zustandsautomaten)
- Interlock-Logiken
- Diagnosemechanismen / Fault Detection

## Nicht im Scope
- Entwicklung eigener Hardware-Treiber
- Optimierung von Hardwarekomponenten
- Messtechnische Analyse von Energieflüssen
- Entwicklung neuartiger Regelungsverfahren

## Coding-Konventionen (verbindlich)
- Dateinamen: snake_case → z.B. pi_controller.c, fault_detection.h
- Funktionen: snake_case mit Modul-Präfix → z.B. pi_ctrl_update(), fsm_tick()
- Typen/Structs: snake_case mit _t Suffix → z.B. pi_ctrl_t, fsm_state_t
- Konstanten und Makros: UPPER_SNAKE_CASE → z.B. PI_CTRL_OUTPUT_MAX
- Jede .c/.h Datei beginnt mit Kommentarblock:
    /*
     * @file    dateiname.c
     * @brief   Kurzbeschreibung
     * @author  Claude Code (AI) / Niklas Grill
     * @date    YYYY-MM-DD
     */
- Alle Kommentare auf Englisch

## Repo-Struktur
- /src          – Quellcode (.c Dateien)
- /include      – Header-Dateien (.h)
- /test         – Unit-Tests (host-side, ohne Hardware)
- /components   – Wiederverwendbare ESP-IDF Komponenten
- /docs         – Dokumentation, Testprotokolle
- platformio.ini – PlatformIO Projektkonfiguration

## Testing
- Unit-Tests laufen auf dem Host-PC (nicht auf dem ESP32)
- Algorithmus-Layer ist so zu implementieren, dass er ohne Hardware testbar ist
- Testfälle je Modul: Sollwertsprünge, Sättigungen, Störgrößen,
  Fehlerzustände, Grenzbedingungen
- Kenngrößen: Reaktionszeit, stationäre Regelabweichung, Fehlererkennungszeit

## Arbeitsweise
- Anforderungen kommen als GitHub Issues
- Code wird ausschließlich als Pull Request eingereicht
- Kein direkter Push auf main
- Commit-Messages auf Englisch, imperative Form
  → "Add PI controller module", "Fix saturation in PI output clamping"