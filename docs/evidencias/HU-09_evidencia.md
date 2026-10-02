# Evidencia de QA y Validación — HU-09: Canal MQTT cifrado con validación del broker

## 1. Resumen de la Historia de Usuario
- **ID**: HU-09
- **Título**: Canal MQTT cifrado con validación del broker
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Cargar certificado CA en el nodo** | `[FW]` | La CA se sube a LittleFS (`/certs/root_ca.pem`) y el nodo la carga (`ca=1`). | **PASS (CA del banco local)** | Sección 3 |
| **Verificar conexión TLS** | `[FW]` | Handshake TLS en 8883 validando CA y nombre del servidor. | **PASS con broker local / PENDIENTE con EMQX real** | Sección 3 |
| **Probar rechazo con certificado inválido** | `[QA]` | Certificado del broker firmado por otra CA → el nodo corta el handshake (`unknown ca`). | **PASS** | Sección 4 |
| **Probar rechazo con host distinto** | `[QA]` | Certificado válido pero emitido para otro host → el nodo corta (`bad certificate`). | **PASS** | Sección 5 |
| **Probar timeout de handshake y reconexión** | `[FW]` | Un puerto que acepta TCP pero no responde TLS se abandona a los 10 s, sin reinicio, y se reintenta. | **PASS** | Sección 6 |

---

## 3. Conexión TLS correcta
```text
12:35:44.908 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:35:45.920 > [MQTT] Conectado (TLS, QoS 1). Acuses en farmacias/ESP32_FARMACIA_01/ack.
12:37:12.309 > wifi=OK rssi=-72 dBm intentos_fallidos=0 retardo=1000ms diag=0 | mqtt=OK ca=1 aprov=1
12:38:41.353 > wifi=OK rssi=-73 dBm intentos_fallidos=0 retardo=1000ms diag=0 | mqtt=OK ca=1 aprov=1
```

## 4. CA ajena: lado broker
```text
1790963139: OpenSSL Error while trying to get the error[0]: error:0A000418:SSL routines::tlsv1 alert unknown ca
1790963139: Client 192.168.18.88 [192.168.18.88:56746] disconnected: protocol error.
1790963144: OpenSSL Error while trying to get the error[0]: error:0A000418:SSL routines::tlsv1 alert unknown ca
```

## 5. Host distinto: lado nodo y lado broker
```text
12:44:54.456 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:44:54.456 > [MQTT] ERROR:   -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).
12:44:59.542 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:44:59.542 > [MQTT] ERROR:   -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).
1790963089: OpenSSL Error while trying to get the error[0]: error:0A000412:SSL routines::ssl/tls alert bad certificate
1790963089: Client 192.168.18.21 [192.168.18.21:50584] disconnected: protocol error.
1790963091: OpenSSL Error while trying to get the error[0]: error:0A000412:SSL routines::ssl/tls alert bad certificate
```

## 6. Timeout de handshake (intentos cada ~10 s, sin [BOOT])
```text
12:46:49.482 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:46:49.482 > [MQTT] ERROR:   -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).
12:46:50.086 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:47:00.030 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
12:47:00.030 > [MQTT] ERROR:   -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).
12:47:00.637 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:47:10.585 > [MQTT] ERROR: Conexion rechazada: lastError=-3 returnCode=0.
```

## 7. Pendiente / fuera de alcance de esta evidencia
- Conexión contra **EMQX Cloud Serverless real** con su CA: pendiente (requiere host, CA y credencial del despliegue).
- En ningún caso el nodo intentó una conexión sin TLS: el puerto 8883 es fijo (HU-14).
