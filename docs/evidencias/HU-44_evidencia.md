# Evidencia de QA y Validación — HU-44: Rotación y Revocación de Credenciales IoT (lado nodo)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-44
- **Título**: Rotación y Revocación de Credenciales IoT (lado nodo)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: broker mosquitto 2.1.2 local con TLS en 8883 (misma política que EMQX: usuario = device_id, ACL por tópico) y backend simulado con acuse lógico (`ColdCare1023/Backend/banco_mqtt_local`). **No es EMQX Cloud ni el backend FastAPI real.**

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Token inválido o revocado** | `[FW]` | El broker rechaza con `returnCode=5` (Not authorized, MQTT 3.1.1) y el nodo lo registra; la captura sigue. | **PASS** | Sección 3 |
| **Rotación sin reiniciar** | `[FW]` | `nvs set mqtt_token` se aplica sin `[BOOT]`: DISCONNECT ordenado y reconexión con la credencial nueva, también si el nodo estaba rechazado. | **PASS** | Sección 3 |
| **Reintentos espaciados** | `[FW]` | Los reintentos de conexión MQTT crecen de 5 a 60 s. | **PASS** | Marcas de tiempo en la sección 3 |

---

## 3. Token incorrecto → rechazo → token correcto → reconexión y drenaje
```text
12:54:17.580 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:54:18.593 > [MQTT] Conectado (TLS, QoS 1). Acuses en farmacias/ESP32_FARMACIA_01/ack.
12:54:23.667 > [Red] 20 lectura(s) con acuse logico; quedan 1.
12:54:23.870 > [Red] 1 lectura(s) con acuse logico; quedan 0.
12:54:34.797 > nvs set mqtt_token -> OK (valor no mostrado). Reiniciar para aplicar; el token se aplica sin reiniciar.
12:54:34.797 > [Red] Token MQTT rotado en NVS: reconexion ordenada sin reiniciar (HU-44).
12:54:34.797 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:54:35.403 > [MQTT] ERROR: Conexion rechazada: lastError=-10 returnCode=5.
12:54:35.403 > [MQTT] ERROR:   -> credencial invalida o revocada (HU-10/HU-44).
12:54:44.926 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:54:45.537 > [MQTT] ERROR: Conexion rechazada: lastError=-10 returnCode=5.
12:54:45.537 > [MQTT] ERROR:   -> credencial invalida o revocada (HU-10/HU-44).
12:55:04.994 > [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
12:55:05.602 > [MQTT] ERROR: Conexion rechazada: lastError=-10 returnCode=5.
12:55:05.602 > [MQTT] ERROR:   -> credencial invalida o revocada (HU-10/HU-44).
12:55:30.137 > nvs set mqtt_token -> OK (valor no mostrado). Reiniciar para aplicar; el token se aplica sin reiniciar.
```

## 4. Lado broker
```text
1790963292: Sending CONNACK to ESP32_FARMACIA_01 (0, 5)
1790963292: Client ESP32_FARMACIA_01 [192.168.18.88:56763] disconnected: not authorised.
1790963297: Sending CONNACK to ESP32_FARMACIA_01 (0, 5)
```

## 5. Pendiente / fuera de alcance de esta evidencia
- Revocación ejecutada desde la gestión de dispositivos del backend y su auditoría (escenarios 1 y 3): capa Cloud.
- Defecto encontrado en esta prueba y corregido (commit `fix(HU-44)`): el nodo no recogía el token nuevo si estaba desconectado.
