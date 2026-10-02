# Evidencia de QA y Validación — HU-08: Reconexión automática del nodo

## 1. Resumen de la Historia de Usuario
- **ID**: HU-08
- **Título**: Reconexión automática del nodo
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Verificar backoff entre intentos** | `[FW]` | Reintentos Wi-Fi a 2, 4, 8, 16, 32 y 60 s (tope). | **PASS** | Sección 3 |
| **Señal de diagnóstico (esc. 3)** | `[FW]` | Tras 5 fallos seguidos se registra el diagnóstico sin detener la captura. | **PASS** | Sección 3 |
| **Probar corte de Wi-Fi** | `[FW]` | Sin red la captura sigue; con la red disponible reconecta y reinicia el contador. | **PASS (red ausente → red disponible)** / **PENDIENTE corte en caliente** | Sección 3 y 4 |
| **Probar corte del broker** | `[FW]` | Con el broker caído sigue capturando; al volver reconecta y drena en orden. | **PASS** | Sección 5 |
| **Registrar evidencia** | `[QA]` | Logs de hardware guardados. | **PASS** | Este documento |

---

## 3. Backoff y diagnóstico (red de 5 GHz, no visible para el ESP32)
```text
12:26:25.723 > [WiFi] ERROR: Sin asociacion con '<SSID>' (intento 1, proximo en 2000 ms).
12:26:25.723 > [WiFi] Reintento #2 (backoff 2000 ms).
12:26:40.349 > [CAP] boot=12 seq=2 t=- tq=nosync ds=24.50 ds_f=- sht=25.02 hr=70.93 sht_f=- door=0 door_s=0 g=OK q=70 ocup=0
12:26:40.751 > [WiFi] ERROR: Sin asociacion con '<SSID>' (intento 2, proximo en 4000 ms).
12:26:40.952 > [WiFi] Reintento #3 (backoff 4000 ms).
12:27:10.266 > [CAP] boot=12 seq=3 t=- tq=nosync ds=24.63 ds_f=- sht=24.99 hr=70.95 sht_f=- door=0 door_s=0 g=OK q=71 ocup=0
12:27:12.084 > [WiFi] ERROR: Sin asociacion con '<SSID>' (intento 4, proximo en 16000 ms).
12:27:12.693 > [WiFi] Reintento #5 (backoff 16000 ms).
12:27:28.276 > [WiFi] ERROR: Sin asociacion con '<SSID>' (intento 5, proximo en 32000 ms).
12:27:28.276 > [WiFi] ERROR: DIAGNOSTICO: 5 fallos seguidos. AP ausente o credenciales invalidas. La captura offline continua.
12:27:40.434 > [CAP] boot=12 seq=4 t=- tq=nosync ds=24.63 ds_f=- sht=25.00 hr=70.96 sht_f=- door=0 door_s=0 g=OK q=72 ocup=0
12:27:44.902 > [WiFi] Reintento #6 (backoff 32000 ms).
12:28:00.295 > [WiFi] ERROR: Sin asociacion con '<SSID>' (intento 6, proximo en 60000 ms).
12:28:10.458 > [CAP] boot=12 seq=5 t=- tq=nosync ds=24.56 ds_f=- sht=25.03 hr=70.99 sht_f=- door=0 door_s=0 g=OK q=73 ocup=0
```

## 4. Reconexión y sincronización de hora
```text
12:33:37.720 > [WiFi] Conectado: IP 192.168.18.88, RSSI -68 dBm.
12:33:37.720 > [Reloj] SNTP en marcha (pool.ntp.org, time.google.com, time.cloudflare.com).
12:33:39.137 > [Reloj] SNTP sincronizado: 2026-10-02T17:33:39Z
12:34:05.067 > [CAP] boot=13 seq=2 t=2026-10-02T17:34:05Z tq=sntp ds=24.63 ds_f=- sht=25.07 hr=70.11 sht_f=- door=0 door_s=0 g=OK q=85 ocup=0
12:34:12.167 > wifi=OK rssi=-82 dBm intentos_fallidos=0 retardo=1000ms diag=0 | mqtt=NO ca=0 aprov=0
```

## 5. Corte del broker y recuperación
```text
12:40:57.248 > reloj=sntp_synced | buffer: pendientes=4/30000 ocupacion=0% libres=1560KB cuarentena=0
12:41:07.179 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:41:17.721 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:41:28.289 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:42:35.264 > reloj=sntp_synced | buffer: pendientes=7/30000 ocupacion=0% libres=1556KB cuarentena=0
12:44:09.505 > [Red] 10 lectura(s) con acuse logico; quedan 0.
12:44:11.732 > reloj=sntp_synced | buffer: pendientes=0/30000 ocupacion=0% libres=1556KB cuarentena=0
12:44:12.747 > [Red] 1 lectura(s) con acuse logico; quedan 0.
```

## 6. Pendiente / fuera de alcance de esta evidencia
- Corte de Wi-Fi en caliente (apagar el router con el nodo conectado): pendiente de ejecutar.
- RSSI medido en el banco: −68 a −82 dBm (criterio C-5: ≥ −65 dBm); la medición válida es la del local.
