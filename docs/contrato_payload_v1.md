# Contrato de mensajes nodo ↔ backend — v1.0

Documento de interfaz entre la capa Edge (Brenda) y la capa Cloud/Backend (Diego).
Fuente de verdad del lado nodo: `src/core/Payload.cpp` (probado en `test/test_core/test_contrato.cpp`).
Esquema Pydantic equivalente: repositorio **ColdCare1023/Backend**, `contrato/telemetria_v1.py`.
Ejemplos generados con el código real del firmware: [`docs/ejemplos/`](ejemplos/).

## Tópicos MQTT (EMQX Cloud Serverless, `mqtts://<host>:8883`)

| Tópico | Sentido | QoS | Contenido |
|---|---|---|---|
| `farmacias/<device_id>/lecturas` | nodo → backend | 1 | Telemetría v1.0 |
| `farmacias/<device_id>/eventos` | nodo → backend | 1 | Eventos de diagnóstico y LWT |
| `farmacias/<device_id>/ack` | backend → nodo | 1 | Acuse lógico tras `COMMIT` |
| `farmacias/<device_id>/ota` | backend → nodo | 1 | Orden de actualización OTA (IOT-004) |

ACL sugerida en EMQX (HU-10): el usuario `<device_id>` solo puede **publicar** en
`farmacias/<device_id>/lecturas` y `.../eventos`, y solo **suscribirse** a `farmacias/<device_id>/ack`
y `.../ota`. Solo la cuenta del backend publica en `farmacias/+/ack` y `farmacias/+/ota`.

## 1. Telemetría (`.../lecturas`)

```json
{
  "schema_version": "1.0",
  "device_id": "ESP32_FARMACIA_01",
  "boot_id": 42,
  "seq_no": 1205,
  "captured_at": "2026-09-25T15:30:00Z",
  "time_quality": "sntp_synced",
  "uptime_s": 36000,
  "temp_ds18b20": 4.25,
  "ds18b20_status": "OK",
  "ds18b20_fault": null,
  "temp_sht31": 21.10,
  "humedad_sht31": 58.50,
  "sht31_status": "OK",
  "sht31_fault": null,
  "door_open": false,
  "door_open_s": 0,
  "door_open_streak_s": null,
  "mc38_status": "installed",
  "power_source": null,
  "sensor_fault": false,
  "firmware_version": "v1.2.0"
}
```

| Campo | Tipo | Regla |
|---|---|---|
| `schema_version` | `"1.0"` | Otra versión → rechazo explícito (HU-15 esc. 4). |
| `device_id` | str 1–50 `[A-Za-z0-9_-]` | Es el usuario MQTT y parte del tópico. |
| `boot_id` | uint32 | Se incrementa en cada arranque (NVS). |
| `seq_no` | uint32 ≥ 1 | Monótono dentro del `boot_id`. **Clave de idempotencia: `device_id + boot_id + seq_no`** (HU-11). Un salto de `seq_no` = muestra no almacenada (ver evento `buffer_saturado`). |
| `captured_at` | ISO 8601 UTC \| `null` | Hora de **captura** (no de envío). `null` si no hay hora confiable. |
| `time_quality` | `sntp_synced` \| `sntp_backfilled` \| `unsynced` | `backfilled`: capturada sin hora y fechada después con el uptime del mismo arranque. |
| `uptime_s` | uint32 | Siempre presente; permite re-fechar lecturas `unsynced`. |
| `temp_ds18b20` | float \| `null` | **Temperatura interna** (la de la regla 2–8 °C). `null` ⇔ `ds18b20_status = "SENSOR_FAULT"`. |
| `ds18b20_fault` | `null` \| `no_response` \| `crc_error` \| `timeout` \| `implausible` \| `power_on_reset` | Evidencia del motivo (HU-01 esc. 2). |
| `temp_sht31`, `humedad_sht31` | float \| `null` | Ambiente exterior. La falla del SHT31 **no** invalida el DS18B20. |
| `sht31_fault` | igual que arriba | Motivo de la primera magnitud que falló. |
| `door_open` | bool \| `null` | ¿Hubo apertura en la ventana? `null` si `mc38_status = "not_installed"`. |
| `door_open_s` | uint16 \| `null` | Segundos abierta en la ventana. |
| `door_open_streak_s` | uint16 \| `null` | Solo si la apertura en curso supera el umbral (60 s por defecto) — HU-04 esc. 3. |
| `mc38_status` | `installed` \| `not_installed` | |
| `power_source` | `mains` \| `backup` \| `null` | `null` si el nodo no tiene sensado de suministro. |
| `sensor_fault` | bool | `true` si DS18B20 o SHT31 fallaron. |
| `firmware_version` | `vX.Y.Z` | Versión con la que se **capturó** la lectura. |
| `simulated` | `true` (opcional) | Solo firmware `demo`. Nunca en producción. |

`received_at` **no** lo envía el nodo: lo agrega el backend al recibir (HU-05 esc. 1).

### Reglas que el backend debe respetar

1. **Nunca** calcular excursión ni inferencia IA con `temp_ds18b20 = null` o `ds18b20_status = "SENSOR_FAULT"` (HU-01 esc. 2–3, HU-21 esc. 4).
2. Aceptar el mensaje con sensor en falla (se registra la falla); rechazar solo mensajes estructuralmente inválidos (HU-15).
3. `simulated = true` → etiquetar como demo; excluir de métricas, entrenamiento y KPI.
4. Deduplicar por `UNIQUE(device_id, boot_id, seq_no)`; si llega la misma identidad con payload distinto, auditar el conflicto (HU-11 esc. 3).

## 2. Acuse lógico (`.../ack`) — backend → nodo

Publicar **después** del `COMMIT` en PostgreSQL (nunca antes):

```json
{"device_id": "ESP32_FARMACIA_01", "boot_id": 42, "seq_no": 1205, "status": "committed",
 "content_sha256": "<SHA-256 en hex de los bytes exactos recibidos en .../lecturas>"}
```

`content_sha256` (IOT-008) es **obligatorio**: un acuse sin él se ignora. El nodo solo libera el
registro si la huella coincide con la de un JSON que él mismo publicó para ese `(boot_id, seq_no)`;
si no coincide, el registro va a **cuarentena** (no se borra) y se cuenta en `acks_huella_ajena`
de `estado_nodo`. Un conflicto de contenido (misma identidad, valores distintos) se acusa como
`rejected` (BE-001), nunca como `duplicate`.

| `status` | Cuándo | Efecto en el nodo |
|---|---|---|
| `committed` | la lectura quedó persistida | libera el registro de LittleFS |
| `duplicate` | ya existía con el mismo contenido (reenvío) | libera el registro |
| `rejected` | rechazo **permanente** ya auditado (esquema, dispositivo no autorizado) | mueve el registro a **cuarentena** en LittleFS (no lo borra) y sigue con el FIFO |

Sin acuse en 8 s el nodo **conserva** el registro y lo reenvía más tarde (con backoff).
El `PUBACK` de MQTT **no** libera nada.
Si PostgreSQL falla, **no** publicar acuse (HU-22 esc. 3).
Se aceptan también `estado` y los valores `commit_confirmado` / `duplicado` / `rechazado`.

## 3. Eventos (`.../eventos`)

```json
{"schema_version":"1.0","device_id":"ESP32_FARMACIA_01","boot_id":42,
 "event_type":"buffer_saturado","occurred_at":"2026-09-25T15:31:00Z",
 "time_quality":"sntp_synced","uptime_s":36060,"detail":{...}}
```

| `event_type` | `detail` | HU |
|---|---|---|
| `arranque` | `reset_reason`, `firmware_version`, `intervalo_s`, `fs_formateado`, `consola_protegida`, `particion`, `imagen_a_prueba`, `certificado_cliente`, `pendientes_recuperados`, `segmentos_con_cola_parcial`, `bytes_parciales_ignorados`, `cursor_reconstruido` | HU-06 esc. 2, QA |
| `online` | `rssi_dbm`, `pendientes`, `firmware_version` | HU-13 esc. 3 |
| `lwt_offline` | *(sin `detail`; lo publica el broker)* | HU-13 esc. 1 |
| `buffer_80` | `ocupacion_pct`, `pendientes`, `capacidad` | HU-06 |
| `buffer_saturado` | `perdidas_total`, `perdidas_episodio`, `episodio_abierto`, `desde_boot_id`, `desde_seq_no`, `desde`, `hasta_boot_id`, `hasta_seq_no`, `hasta`, `causa` | HU-06 esc. 3, HU-48 esc. 2 |
| `registros_en_cuarentena` | `corruptos`, `rechazados_backend`, `invalidos`, `cuarentena_total` | HU-06 esc. 2 |
| `wifi_reconexion_prolongada` | `intentos_fallidos`, `duracion_s`, `umbral_intentos` | HU-08 esc. 3 |
| `credencial_rotada` | — | HU-44 esc. 2 |
| `puerta` | `door_open` (true = abierta); `occurred_at` es la hora del cambio | HU-04 esc. 1 |
| `conmutacion_respaldo` / `suministro_restablecido` | `desde`/`hacia` · `duracion_respaldo_s` | HU-52 esc. 1 (solo con sensado) |
| `respaldo_autonomia_critica` | `transcurrido_s`, `autonomia_medida_min`, `base` | HU-52 esc. 3 (estimación) |
| `config_modificada` | `clave` (nombre de la clave NVS, nunca el valor), `accion` (`set`/`borrar`; `generar` para `mqtt_key`) | IOT-002, IOT-006 |
| `reinicio_manual` | — (se publica antes del DISCONNECT ordenado, que no dispara el LWT) | IOT-002 |
| `consola_desbloqueada` | — (desbloqueo con la clave de servicio, 10 min) | IOT-002 |
| `consola_desbloqueo_fallido` | `intentos` (fallos seguidos al activarse el freno) | IOT-002 |
| `ota_iniciada` | `version` (descarga en curso) | IOT-004 |
| `ota_instalada` | `version`, `resultado` (`instalada`); el nodo reinicia para arrancarla a prueba | IOT-004 |
| `ota_rechazada` | `motivo` (campo inválido de la orden, o `imagen_en_prueba`) o `version` + `resultado` (`version_no_posterior`) | IOT-004 |
| `ota_fallida` | `version`, `resultado` (`sin_ca`, `descarga`, `tamano_distinto`, `huella_distinta`, `escritura`) | IOT-004 |
| `ota_confirmada` | `firmware_version`, `particion` (la imagen nueva superó el autodiagnóstico) | IOT-004 |
| `ota_revertida` | `firmware_version`, `particion` (el bootloader volvió a la imagen anterior) | IOT-004 |
| `estado_nodo` (cada 5 min) | `pendientes`, `ocupacion_pct`, `capacidad`, `perdidas_total`, `cuarentena`, `ultimo_seq_no`, `rssi_dbm`, `heap_libre`, `heap_minimo`, `intervalo_s`, `pubacks_sin_ack`, `acks_huella_ajena`, `eventos_descartados`, `reloj_sntp`, `saltos_reloj_rechazados` | HU-48 esc. 1, C-5 |

## 3.1 Orden OTA (`.../ota`) — backend → nodo (IOT-004)

```json
{"device_id":"ESP32_FARMACIA_01","version":"1.3.0",
 "url":"https://firmware.example.org/coldcare-1.3.0.bin",
 "sha256":"<64 hex en minúsculas>","tamano":1100000}
```

El nodo la acepta solo si `device_id` es el suyo, `version` es `X.Y.Z` y **posterior** a la que
corre, `url` es HTTPS (≤ 256 caracteres, sin espacios ni comillas), `sha256` tiene 64 hex en
minúsculas y `tamano` cabe en la partición de aplicación (≤ `0x130000`). Descarga sin seguir
redirecciones, con la misma CA de confianza que usa para el broker (el servidor de descargas debe
estar en el paquete de `data/certs/root_ca.pem`), exige `Content-Length == tamano` y solo activa la
imagen si su SHA-256 coincide con la orden. Con Secure Boot (entorno `produccion`) el bootloader
exige además la firma. La imagen nueva arranca **a prueba**: se confirma cuando captura una lectura
y conecta con el broker; si no lo logra en 15 min (`OTA_PLAZO_VERIFICACION_MS`) se revierte.
Mientras una imagen está a prueba no se instala otra.

El backend deja el despliegue `enviado` al publicar la orden y lo cierra con el evento del nodo:
`ota_confirmada` → `exitoso`; `ota_fallida`/`ota_rechazada` → `fallido`; `ota_revertida` → `revertido`.

## 4. Diferencias con el contrato que hoy valida el backend

El backend actual (`backend/src/infrastructure/mqtt/payload_schema.py`) valida el contrato
anterior (`schema_version` entero `1`, `timestamp`, `temperatura_interna`, `reading_id`, …) con
`extra="forbid"`. **Con ese esquema rechazaría el 100 % de los mensajes v1.0.** Mapeo para Diego:

| v1.0 (nodo nuevo) | contrato anterior |
|---|---|
| `schema_version: "1.0"` (texto) | `schema_version: 1` (entero) |
| `captured_at` (puede ser `null`) | `timestamp` (obligatorio) |
| `temp_ds18b20` + `ds18b20_status`/`ds18b20_fault` | `temperatura_interna` + `estado_temperatura_interna` |
| `temp_sht31`, `humedad_sht31` + `sht31_status`/`sht31_fault` | `temperatura_ambiental`, `humedad_ambiental` + estados |
| `door_open` | `apertura_refrigerador` |
| `door_open_s` | `duracion_apertura_segundos` |
| `mc38_status: installed` | `mc38_status: ok` |
| acuse por `boot_id` + `seq_no` + `status` | acuse por `reading_id` + `estado` |
| — | `reading_id`, `estado_conectividad` (ya no se envían) |
| `uptime_s`, `sensor_fault`, `door_open_streak_s`, `power_source`, `simulated` | *(nuevos)* |

Además: el acuse actual se genera con `json.dumps` (`"clave": valor`, con espacio). El
firmware anterior buscaba `"reading_id":"` literal y **no lo habría encontrado nunca**. El
parser nuevo tolera espacios (`test_json_lee_formato_python_con_espacios`).
