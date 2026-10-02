# Evidencia de QA y Validación — HU-04: Detección de apertura de puerta (MC-38, opcional)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-04
- **Título**: Detección de apertura de puerta (MC-38, opcional)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Conectar MC-38 al pin acordado** | `[HW]` | Reed switch en GPIO 27 a GND con pull-up interno. | **PASS** | `[MC38] GPIO27, estado inicial: cerrada.` |
| **Verificar evento de apertura** | `[FW]` | Cada cambio estable genera un evento `puerta` con la hora del cambio; la telemetría resume la ventana (`door_open`, `door_open_s`). | **PASS** | Sección 3 y 4 |
| **Apertura prolongada (esc. 3)** | `[FW]` | Con la puerta abierta más de 60 s el payload agrega `door_open_streak_s`. | **PASS** | Lectura a 48 s: `null`; a 78 s: `78` (sección 4) |
| **Probar codificación sin MC-38** | `[FW]` | `door_open = null`, `mc38_status = not_installed`. | **PASS (host)** | `test_payload_sin_mc38_door_open_null`; compila con `-DMC38_INSTALADO=0` |
| **Registrar evidencia** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |

---

## 3. Apertura de ~8 s y apertura de 82 s (monitor serie)
```text
13:21:40.202 > [MC38] Puerta ABIERTA.
13:21:40.202 > [Evento] Encolado 'puerta'.
13:21:44.878 > [CAP] boot=20 seq=56 t=2026-10-02T18:21:44Z tq=sntp ds=24.75 ds_f=- sht=25.83 hr=68.32 sht_f=- door=1 door_s=4 g=OK q=9 ocup=0
13:21:48.527 > [MC38] Puerta cerrada.
13:21:48.527 > [Evento] Encolado 'puerta'.
13:21:56.263 > [MC38] Puerta ABIERTA.
13:21:56.264 > [Evento] Encolado 'puerta'.
13:22:44.977 > [CAP] boot=20 seq=58 t=2026-10-02T18:22:44Z tq=sntp ds=24.75 ds_f=- sht=25.80 hr=68.31 sht_f=- door=1 door_s=30 g=OK q=11 ocup=0
13:23:14.832 > [CAP] boot=20 seq=59 t=2026-10-02T18:23:14Z tq=sntp ds=24.75 ds_f=- sht=25.79 hr=68.33 sht_f=- door=1 door_s=30 g=OK q=12 ocup=0
13:23:18.883 > [MC38] Puerta cerrada.
13:23:18.883 > [Evento] Encolado 'puerta'.
13:23:44.864 > [CAP] boot=20 seq=60 t=2026-10-02T18:23:44Z tq=sntp ds=24.75 ds_f=- sht=25.82 hr=68.29 sht_f=- door=1 door_s=4 g=OK q=13 ocup=0
13:24:14.948 > [CAP] boot=20 seq=61 t=2026-10-02T18:24:14Z tq=sntp ds=24.75 ds_f=- sht=25.85 hr=68.27 sht_f=- door=0 door_s=0 g=OK q=14 ocup=0
```

## 4. Payloads persistidos (door_open, door_open_s, door_open_streak_s)
```text
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":56,"captured_at":"2026-10-02T18:21:44Z","time_quality":"sntp_synced","uptime_s":1650,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.83,"humedad_sht31":68.32,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":4,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":57,"captured_at":"2026-10-02T18:22:14Z","time_quality":"sntp_synced","uptime_s":1680,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.82,"humedad_sht31":68.24,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":22,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":58,"captured_at":"2026-10-02T18:22:44Z","time_quality":"sntp_synced","uptime_s":1710,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.80,"humedad_sht31":68.31,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":30,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":59,"captured_at":"2026-10-02T18:23:14Z","time_quality":"sntp_synced","uptime_s":1740,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.79,"humedad_sht31":68.33,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":30,"door_open_streak_s":78,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":60,"captured_at":"2026-10-02T18:23:44Z","time_quality":"sntp_synced","uptime_s":1770,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.82,"humedad_sht31":68.29,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":4,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":61,"captured_at":"2026-10-02T18:24:14Z","time_quality":"sntp_synced","uptime_s":1800,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.85,"humedad_sht31":68.27,"sht31_status":"OK","sht31_fault":null,"door_open":false,"door_open_s":0,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
```

## 5. Pendiente / fuera de alcance de esta evidencia
- Los eventos `puerta` se publican cuando hay conexión; si el nodo se reinicia sin red antes de publicarlos se pierden (el resumen por ventana sí queda en LittleFS).
