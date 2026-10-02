# Evidencia de QA y Validación — HU-02: Captura de temperatura ambiental (SHT31)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-02
- **Título**: Captura de temperatura ambiental (SHT31)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Conectar SHT31 por I²C** | `[HW]` | El sensor responde en 0x44 (SDA 21 / SCL 22). | **PASS** | `[SHT31] Detectado en 0x44 (SDA=21, SCL=22)` |
| **Verificar lectura de temperatura** | `[FW]` | La temperatura ambiente entra al mismo payload identificado. | **PASS** | Sección 3 |
| **Probar timeout y reintentos** | `[FW]` | Sin respuesta I²C: 3 intentos y luego `SENSOR_FAULT`. | **PASS** | `SENSOR_FAULT tras 3 intentos` (sección 4) |
| **Probar falla sin afectar DS18B20** | `[FW]` | Con el SHT31 desconectado, `ds` sigue válido en la misma lectura. | **PASS** | Sección 4 y 5 |
| **Registrar evidencia de escenarios** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |

---

## 3. Detección y lectura
```text
12:35:42.272 > [SHT31] Detectado en 0x44 (SDA=21, SCL=22).
12:35:42.272 > [CAP] boot=16 seq=1 t=- tq=nosync ds=24.63 ds_f=- sht=25.14 hr=70.01 sht_f=- door=0 door_s=0 g=OK q=2 ocup=0
12:36:12.292 > [CAP] boot=16 seq=2 t=2026-10-02T17:36:12Z tq=sntp ds=24.63 ds_f=- sht=25.11 hr=69.82 sht_f=- door=0 door_s=0 g=OK q=3 ocup=0
```

## 4. SDA desconectado en caliente y recuperación
```text
13:17:44.902 > [CAP] boot=20 seq=48 t=2026-10-02T18:17:44Z tq=sntp ds=24.81 ds_f=- sht=25.87 hr=68.71 sht_f=- door=0 door_s=0 g=OK q=1 ocup=0
13:18:14.749 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
13:18:14.950 > [CAP] boot=20 seq=49 t=2026-10-02T18:18:14Z tq=sntp ds=24.81 ds_f=- sht=NULL hr=NULL sht_f=no_response door=0 door_s=0 g=OK q=2 ocup=0
13:18:44.778 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
13:18:44.979 > [CAP] boot=20 seq=50 t=2026-10-02T18:18:44Z tq=sntp ds=24.81 ds_f=- sht=NULL hr=NULL sht_f=no_response door=0 door_s=0 g=OK q=3 ocup=0
13:19:14.798 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
13:19:44.862 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
13:20:14.739 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
13:20:44.798 > [SHT31] ERROR: SENSOR_FAULT tras 3 intentos: T=no_response HR=no_response
```

## 5. Payload persistido: SHT31 en falla, DS18B20 intacto
```text
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":49,"captured_at":"2026-10-02T18:18:14Z","time_quality":"sntp_synced","uptime_s":1440,"temp_ds18b20":24.81,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":null,"humedad_sht31":null,"sht31_status":"SENSOR_FAULT","sht31_fault":"no_response","door_open":false,"door_open_s":0,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":true,"firmware_version":"v1.2.0"}
```

## 6. Pendiente / fuera de alcance de esta evidencia
- Contraste metrológico de la temperatura ambiente (MET-02): pendiente del piloto.
