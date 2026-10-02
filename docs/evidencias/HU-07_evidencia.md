# Evidencia de QA y Validación — HU-07: Sincronización de Buffer Offline con Acuse Lógico

## 1. Resumen de la Historia de Usuario
- **ID**: HU-07
- **Título**: Sincronización de Buffer Offline con Acuse Lógico
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Verificar borrado solo tras acuse** | `[FW]` | Con PUBACK del broker pero sin acuse lógico, el registro se conserva y se reintenta siempre el más antiguo. | **PASS** | Sección 3 |
| **Probar reenvío FIFO al reconectar** | `[FW]` | Al volver el backend/broker los pendientes salen en orden de `boot_id`/`seq_no` y el buffer llega a 0. | **PASS** | Sección 4 y 5 |
| **Verificar acuse lógico por lectura** | `[BE]` | El backend publica `committed` por cada lectura tras el COMMIT. | **PASS con backend simulado / PENDIENTE con FastAPI real** | Sección 5 |
| **Registrar evidencia** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |

---

## 3. Regla de oro: PUBACK sí, acuse no → se conserva
```text
12:35:54.043 > [Tx] ERROR: PUBACK si, acuse logico no (boot=15 seq=1): se conserva en LittleFS.
12:36:02.168 > [Tx] ERROR: PUBACK si, acuse logico no (boot=15 seq=1): se conserva en LittleFS.
12:36:12.090 > [Tx] ERROR: PUBACK si, acuse logico no (boot=15 seq=1): se conserva en LittleFS.
12:36:32.180 > [Tx] ERROR: PUBACK si, acuse logico no (boot=15 seq=1): se conserva en LittleFS.
12:37:12.108 > [Tx] ERROR: PUBACK si, acuse logico no (boot=15 seq=1): se conserva en LittleFS.
12:37:12.309 > reloj=sntp_synced | buffer: pendientes=4/30000 ocupacion=0% libres=1560KB cuarentena=0
```

## 4. Drenaje al encender el backend y tras el corte del broker
```text
12:38:05.430 > [Red] 6 lectura(s) con acuse logico; quedan 0.
12:38:12.529 > [Red] 1 lectura(s) con acuse logico; quedan 0.
12:38:41.353 > reloj=sntp_synced | buffer: pendientes=0/30000 ocupacion=0% libres=1560KB cuarentena=0
12:38:42.567 > [Red] 1 lectura(s) con acuse logico; quedan 0.
12:40:57.248 > reloj=sntp_synced | buffer: pendientes=4/30000 ocupacion=0% libres=1560KB cuarentena=0
12:42:35.264 > reloj=sntp_synced | buffer: pendientes=7/30000 ocupacion=0% libres=1556KB cuarentena=0
12:44:09.505 > [Red] 10 lectura(s) con acuse logico; quedan 0.
12:44:11.732 > reloj=sntp_synced | buffer: pendientes=0/30000 ocupacion=0% libres=1556KB cuarentena=0
```

## 5. Backend simulado: orden de llegada y acuse
```text
12:38:04 ✓ ('ESP32_FARMACIA_01', 15, 1) 24.63 °C: committed
12:38:04 ✓ ('ESP32_FARMACIA_01', 16, 1) 24.63 °C: committed
12:38:04 ✓ ('ESP32_FARMACIA_01', 16, 2) 24.63 °C: committed
12:38:04 ✓ ('ESP32_FARMACIA_01', 16, 3) 24.63 °C: committed
12:38:04 ✓ ('ESP32_FARMACIA_01', 16, 4) 24.63 °C: committed
12:38:05 ✓ ('ESP32_FARMACIA_01', 16, 5) 24.63 °C: committed
12:38:12 ✓ ('ESP32_FARMACIA_01', 16, 6) 24.69 °C: committed
12:38:42 ✓ ('ESP32_FARMACIA_01', 16, 7) 24.63 °C: committed
12:44:07 ✓ ('ESP32_FARMACIA_01', 16, 8) 24.63 °C: committed
12:44:07 ✓ ('ESP32_FARMACIA_01', 16, 9) 24.63 °C: committed
12:44:07 ✓ ('ESP32_FARMACIA_01', 16, 10) 24.69 °C: committed
12:44:07 ✓ ('ESP32_FARMACIA_01', 16, 11) 24.63 °C: committed
12:44:07 ✓ ('ESP32_FARMACIA_01', 16, 12) 24.63 °C: committed
12:44:08 ✓ ('ESP32_FARMACIA_01', 16, 13) 24.63 °C: committed
```

## 6. Pendiente / fuera de alcance de esta evidencia
- Acuse emitido por el backend FastAPI real (formato v1.0 por `boot_id` + `seq_no`): pendiente de la capa Cloud.
- El backend simulado persistió 107 lecturas sin duplicados ni conflictos durante la sesión.
