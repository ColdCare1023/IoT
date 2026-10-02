# Evidencia de QA y Validación — HU-03: Captura de humedad relativa (SHT31)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-03
- **Título**: Captura de humedad relativa (SHT31)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Verificar lectura de humedad** | `[FW]` | La humedad relativa (%HR) se lee y viaja en el payload. | **PASS** | `hr=` en `[CAP]` y `humedad_sht31` en el payload (sección 3) |
| **Confirmar que no tiene regla de riesgo propia** | `[FW]` | El firmware solo transporta la humedad; no evalúa ningún umbral ni clase de riesgo. | **PASS** | Inspección de `src/` (ninguna regla sobre `humShtCenti`) |
| **Probar descarte fuera de 0-100 %** | `[FW]` | Valores fuera de 0–100 %HR se rechazan. | **PASS (host)** | `test_sht31_humedad_extremos_validos` + `validar_contrato.py::test_humedad_fuera_de_0_100_se_rechaza` |
| **Registrar evidencia de escenarios** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |

---

## 3. Lecturas de humedad en hardware
```text
13:16:44.825 > [CAP] boot=20 seq=46 t=2026-10-02T18:16:44Z tq=sntp ds=24.81 ds_f=- sht=25.79 hr=68.58 sht_f=- door=1 door_s=3 g=OK q=1 ocup=0
13:21:44.878 > [CAP] boot=20 seq=56 t=2026-10-02T18:21:44Z tq=sntp ds=24.75 ds_f=- sht=25.83 hr=68.32 sht_f=- door=1 door_s=4 g=OK q=9 ocup=0
13:24:14.948 > [CAP] boot=20 seq=61 t=2026-10-02T18:24:14Z tq=sntp ds=24.75 ds_f=- sht=25.85 hr=68.27 sht_f=- door=0 door_s=0 g=OK q=14 ocup=0
```

## 4. Payload persistido
```text
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":20,"seq_no":61,"captured_at":"2026-10-02T18:24:14Z","time_quality":"sntp_synced","uptime_s":1800,"temp_ds18b20":24.75,"ds18b20_status":"OK","ds18b20_fault":null,"temp_sht31":25.85,"humedad_sht31":68.27,"sht31_status":"OK","sht31_fault":null,"door_open":false,"door_open_s":0,"door_open_streak_s":null,"mc38_status":"installed","power_source":null,"sensor_fault":false,"firmware_version":"v1.2.0"}
```

## 5. Pendiente / fuera de alcance de esta evidencia
- La precisión ±2 %HR es especificación del fabricante; el contraste (MET-02) es del piloto.
