# ColdCare IoT — firmware del nodo Edge (ESP32)

Firmware del nodo de monitoreo de cadena de frío (ESP32 DevKitC V4, PlatformIO, Arduino core 2.0.17).
Responsable de la capa Edge: **Brenda Lucía Gamio Upiachihua**.

La historia se organiza en **un commit por historia de usuario** del Product Backlog.

```bash
pio run -e esp32dev -t upload     # firmware
pio test -e native                # pruebas de src/core (Windows sin MinGW: scripts\tests_nativos_msvc.cmd)
pio device monitor -e esp32dev
```
