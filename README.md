# ColdCare IoT — firmware del nodo Edge (ESP32)

Nodo de monitoreo de la cadena de frío de medicamentos termolábiles: ESP32 DevKitC V4 + DS18B20 (interior) +
SHT31 (ambiente) + MC-38 (puerta, opcional) + Mini UPS 5 V. PlatformIO, Arduino core 2.0.17 (ESP-IDF 4.4).
Capa Edge — responsable: **Brenda Lucía Gamio Upiachihua**.

La historia del repositorio tiene **un commit por historia de usuario** del Product Backlog; cada commit
compila para el ESP32 y pasa sus pruebas nativas.

## Estado verificado

| Verificación | Resultado |
|---|---|
| Compilación `esp32dev` y `demo` (y variantes sin MC-38 / con sensado de energía) | ✅ sin warnings propios · RAM 14,6 % · Flash 79,6 % |
| Pruebas nativas de `src/core/` (Unity) | ✅ 77/77 |
| Contrato v1.0 validado con el esquema Pydantic del backend | ✅ (repo ColdCare1023/Backend, `contrato/`) |
| Prueba en hardware real con sensores | ⏳ pendiente |

## Historias de usuario → módulos

| HU | Qué implementa | Módulos |
|---|---|---|
| HU-01 | DS18B20 con CRC, timeout y `SENSOR_FAULT` con motivo; `boot_id`/`seq_no`; hora solo por SNTP | `sensors/DS18B20Sensor`, `core/Sensores`, `core/Tiempo`, `system/Reloj` |
| HU-02 | SHT31 temperatura (I²C, CRC Sensirion, reintentos), falla aislada del DS18B20 | `sensors/SHT31Sensor` |
| HU-03 | SHT31 humedad relativa, validación 0–100 %HR | `core/Sensores` |
| HU-04 | MC-38 con antirrebote 50 ms, apertura prolongada, `not_installed` | `sensors/MC38Sensor`, `core/Antirrebote` |
| HU-05 | Contrato de telemetría v1.0, `null` + `SENSOR_FAULT`, tope 768 B | `core/Payload`, `core/Registro`, `core/Json`, [`docs/contrato_payload_v1.md`](docs/contrato_payload_v1.md) |
| HU-06 | Buffer offline FIFO en LittleFS: 80 %, saturación sin sobrescribir, contador persistente, recuperación tras corte | `core/ColaLecturas`, `storage/BufferLittleFS` |
| HU-07 | Liberación **solo** con acuse lógico del backend (no con PUBACK), cuarentena, reanudación FIFO | `core/Ack`, `network/TransmisorMQTT` |
| HU-08 | Wi-Fi con backoff 1→60 s y señal de diagnóstico; consola de aprovisionamiento | `network/WiFiManager`, `core/Backoff` |
| HU-09 | MQTT sobre TLS con CA y hostname validados, sin fallback | `network/MQTTManager` |
| HU-13 | LWT, evento `online`, DISCONNECT ordenado, eventos de diagnóstico | `network/MQTTManager`, `system/Eventos` |
| HU-14 | Solo 8883: puerto no configurable, host sin esquema ni puerto | `config.h`, `core/Credenciales` |
| HU-21 | Firmware `demo` con excursión simulada (`simulated: true`) | `main.cpp` (`#if MODO_DEMO`) |
| HU-43 | `device_id` por NVS para alta/reemplazo | `system/Configuracion` |
| HU-44 | Rotación/revocación del token sin reiniciar | `main.cpp`, `system/Configuracion` |
| HU-48 | Evento `estado_nodo` (pendientes, pérdidas, RSSI) | `main.cpp` |
| HU-52 | Continuidad ante corte (Mini UPS): sin ADC en GPIO 34, sensado digital opcional | `system/PowerBackup` |

Decisiones y desviaciones documentadas (MQTT 3.1.1 en vez de 5.0, registros binarios, cuarentena…):
[`docs/decisiones_tecnicas.md`](docs/decisiones_tecnicas.md).

## Uso

```bash
pio run -e esp32dev -t upload        # firmware del local
pio run -e esp32dev -t uploadfs      # CA de EMQX en data/certs/root_ca.pem (antes de instalar: borra el buffer)
pio device monitor -e esp32dev       # guarda el log con marca de tiempo (filtros time, log2file)
pio test -e native                   # pruebas (Windows sin MinGW: scripts\tests_nativos_msvc.cmd)
pio run -e demo -t upload            # SOLO para la demostración (ESP32_DEMO_01)
```

Aprovisionamiento por consola serie (sin secretos en el repositorio; los valores nunca se imprimen):

```
nvs set wifi_ssid <red>
nvs set wifi_pass <clave>
nvs set mqtt_host <xxxxxxxx.ala.us-east-1.emqxsl.com>
nvs set mqtt_token <token del dispositivo>
nvs set device_id ESP32_FARMACIA_01
reiniciar
```

Otros comandos: `estado`, `rssi` (protocolo C-5), `ayuda`.

## Pines (ESP32 DevKitC V4)

| Señal | GPIO | Nota |
|---|---|---|
| DS18B20 DQ | 4 | pull-up externo 4,7 kΩ a 3V3, 3 hilos |
| SHT31 SDA / SCL | 21 / 22 | dirección 0x44 |
| MC-38 | 27 | pull-up interno, contacto a GND |
| Sensado de red (opcional) | 26 | divisor 10 kΩ/18 kΩ desde el 5 V anterior al UPS |
| Botón demo | 0 (BOOT) | solo firmware `demo` |

## Build flags por nodo

| Flag | Defecto | Uso |
|---|---|---|
| `DEVICE_ID` | `ESP32_FARMACIA_01` | Identidad (NVS `device_id` la reemplaza) |
| `MC38_INSTALADO` | `1` | `0` sin sensor de puerta |
| `SENSADO_ENERGIA_INSTALADO` | `0` | `1` con el divisor a GPIO 26 |
| `AUTONOMIA_RESPALDO_MEDIDA_MIN` | `0` | Autonomía medida en el ensayo C-6 |
| `BUFFER_MAX_SEGMENTOS` | `300` | Reducir solo para pruebas de saturación |
| `LOG_NIVEL_INFO` | `1` | `0` silencia `LOG_I` (`[CAP]` y `[BOOT]` se mantienen) |
