# Evidencia de QA y Validación — HU-01: Captura de temperatura interna (DS18B20)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-01
- **Título**: Captura de temperatura interna (DS18B20)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Conectar DS18B20 con pull-up 4,7 kΩ** | `[HW]` | La sonda responde en el bus 1-Wire de GPIO 4. | **PASS** | `[DS18B20] Sonda detectada en GPIO4 (12 bits)` |
| **Verificar lectura con device_id, boot_id, seq_no** | `[FW]` | Cada lectura lleva `device_id`, `boot_id` (sube en cada arranque), `seq_no` correlativo, `captured_at` UTC y `time_quality`. | **PASS** | Líneas `[CAP]` y payload persistido (sección 3 y 5) |
| **Probar SENSOR_FAULT con sensor desconectado** | `[FW]` | Con el cable DQ desconectado en caliente la lectura sale `null` + `SENSOR_FAULT` + `no_response`; el ciclo sigue y la sonda se recupera sin reiniciar. | **PASS** | Sección 4 |
| **Probar descarte de valor fuera de rango** | `[FW]` | −56 °C, 127 °C y 85 °C de power-on se marcan `implausible` / `power_on_reset`. | **PASS (host)** | `test_ds18b20_fuera_de_rango_fisico_es_implausible`, `test_ds18b20_valor_power_on_85_grados_es_falla` (77/77 Unity). No reproducible en protoboard. |
| **Registrar evidencia de escenarios 1-3** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |
| **Dejar MAE y sesgo abiertos para validación** | `[QA]` | El escenario 4 se mide en el piloto con referencia calibrada. | **ABIERTO** | Protocolo MET-01 (OE4.I1) |

---

## 3. Arranque y lecturas identificadas (escenario 1)
```text
12:35:42.069 > [BOOT] boot_id=16 reset=SOFTWARE fw=v1.2.0 device=ESP32_FARMACIA_01 intervalo_s=30 mc38=1 sensado_energia=0 demo=0 credenciales=NVS
12:35:42.272 > [DS18B20] Sonda detectada en GPIO4 (12 bits).
12:35:42.272 > [CAP] boot=16 seq=1 t=- tq=nosync ds=24.63 ds_f=- sht=25.14 hr=70.01 sht_f=- door=0 door_s=0 g=OK q=2 ocup=0
12:36:12.292 > [CAP] boot=16 seq=2 t=2026-10-02T17:36:12Z tq=sntp ds=24.63 ds_f=- sht=25.11 hr=69.82 sht_f=- door=0 door_s=0 g=OK q=3 ocup=0
12:36:42.342 > [CAP] boot=16 seq=3 t=2026-10-02T17:36:42Z tq=sntp ds=24.63 ds_f=- sht=25.13 hr=69.89 sht_f=- door=0 door_s=0 g=OK q=4 ocup=0
```

## 4. Sonda desconectada en caliente (escenarios 2 y 3)
```text
13:16:44.825 > [CAP] boot=20 seq=46 t=2026-10-02T18:16:44Z tq=sntp ds=24.81 ds_f=- sht=25.79 hr=68.58 sht_f=- door=1 door_s=3 g=OK q=1 ocup=0
13:17:14.844 > [CAP] boot=20 seq=47 t=2026-10-02T18:17:14Z tq=sntp ds=NULL ds_f=no_response sht=25.80 hr=68.59 sht_f=- door=0 door_s=0 g=OK q=1 ocup=0
13:17:44.699 > [DS18B20] Sonda recuperada tras una falla previa.
13:17:44.902 > [CAP] boot=20 seq=48 t=2026-10-02T18:17:44Z tq=sntp ds=24.81 ds_f=- sht=25.87 hr=68.71 sht_f=- door=0 door_s=0 g=OK q=1 ocup=0
```

## 5. Payload persistido por el backend simulado: falla registrada, sin temperatura (no hay excursión posible)
```text
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":46,"captured_at":"2026-10-02T18:16:44Z","time_quality":"sntp_synced","uptime_s":1350,"temp_ds18b20":24.81,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.79,"humedad_sht31":68.58,"sht31_status":"OK","sht31_fault":null,"door_open":true,"door_open_s":3,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":47,"captured_at":"2026-10-02T18:17:14Z","time_quality":"sntp_synced","uptime_s":1380,"temp_ds18b20":null,"ds18b20_status":"SENSOR_FAULT","ds18b20_fault":"no_response","temp_sht31":25.80,"humedad_sht31":68.59,"sht31_status":"OK","sht31_fault":null,"door_open":false,"door_open_s":0,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":true,"firmware_version":"v1.2.0"}
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":48,"captured_at":"2026-10-02T18:17:44Z","time_quality":"sntp_synced","uptime_s":1410,"temp_ds18b20":24.81,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.87,"humedad_sht31":68.71,"sht31_status":"OK","sht31_fault":null,"door_open":false,"door_open_s":0,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
```

## 6. Pendiente / fuera de alcance de esta evidencia
- Escenario 4 (MAE y sesgo frente a referencia calibrada): pendiente del piloto.
- El intervalo de muestreo definitivo se fija tras el piloto (hoy 30 s, ajustable por NVS).
