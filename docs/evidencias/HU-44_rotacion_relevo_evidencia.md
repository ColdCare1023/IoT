# Evidencia de QA y Validación — HU-44: rotación con token de relevo y rotación por compromiso (ADR-17)

## 1. Resumen
- **ID**: HU-44 (rotación de credenciales IoT) · ADR-17 (token de relevo) · ADR-18 (particiones)
- **Fecha de ejecución**: 27/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 en protoboard (`ESP32_FARMACIA_01`), firmware `esp32dev` en
  `a220a80` (particiones de ADR-18), `intervalo_s = 10` (banco), DS18B20, SHT31 y MC-38 conectados.
- **Entorno**: pila local `Backend/infra-local` en `688a0e2` (`develop`): EMQX 5.8 con autenticación
  integrada y TLS 8883, FastAPI y PostgreSQL 16. El backend se ejecutó con `BROKER_USUARIOS_MODO=manual`
  (el modo previsto para EMQX Cloud Serverless) mediante un archivo de sobrescritura temporal de Compose.
- **Limitación**: el paso «cambiar la contraseña en la consola de EMQX» se hizo con la API de
  administración del EMQX local (`PUT /authentication/password_based:built_in_database/users/{id}`), que
  tiene el mismo efecto sobre el broker. **No es la consola de EMQX Cloud Serverless.**
- Los tokens se cargaron por la consola serie sin mostrarse (el firmware imprime «valor no mostrado»).

---

## 2. Checklist de Verificación y Resultados

| Tarea | Criterio verificado | Estado | Evidencia |
|---|---|---|---|
| **Relevo rechazado mientras la consola está pendiente** | Con el relevo cargado y el broker aún con el token anterior, el nodo prueba el relevo, recibe CONNACK 4 y entra con el vigente sin perder lecturas. | **PASS** | Secciones 3 y 4 |
| **Relevo aceptado al confirmar la consola** | Tras cambiar la contraseña y confirmar, el backend expulsa la sesión y el nodo entra con el relevo, lo promueve y publica `credencial_rotada` con `origen: relevo`. | **PASS** (0,5 s sin conexión) | Sección 4 |
| **Rotación por compromiso: retención sin acuse** | Desde la rotación y hasta confirmar, el backend no persiste lecturas ni envía acuse; el nodo las conserva en LittleFS. | **PASS** | Sección 5 |
| **Entrega de lo retenido al confirmar** | Al confirmar, el nodo entra con el relevo y entrega lo retenido con su hora de captura, sin huecos ni duplicados. | **PASS** (28/28 lecturas) | Sección 5 |
| **SIN_DATOS durante la retención** | El incidente se abre tras 3 × intervalo y se resuelve al volver las lecturas. | **PASS** (abre a 30 s, se resuelve al confirmar) | Sección 5 |
| **Capacidad real de LittleFS (ADR-18)** | `estado_nodo` publica `fs_total_bytes` ≥ 1 310 720 B. | **PASS** (1 507 328 B) | Sección 6 |

---

## 3. Rotación preventiva: relevo rechazado (consola pendiente)

`POST /api/dispositivos/ESP32_FARMACIA_01/credenciales/rotar` con `causa: preventiva` responde
`credencial_aprovisionada: null` y el mensaje de tres pasos (relevo en el nodo, consola, confirmación).
Al cargar el relevo:

```text
nvs set mqtt_relevo -> OK (valor no mostrado). Reiniciar para aplicar; el token se aplica sin reiniciar.
[Red] Token de relevo pendiente: reconexion ordenada para probarlo (HU-44).
[MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
[MQTT] ERROR: Conexion rechazada: lastError=-10 returnCode=4.
[MQTT] ERROR:   -> credencial invalida o revocada (HU-10/HU-44).
[Red] Broker rechazo el token de relevo (CONNACK 4): se sigue con el vigente (HU-44).
[MQTT] Conectado (TLS, QoS 1). Acuses en farmacias/ESP32_FARMACIA_01/ack.
[Red] 1 lectura(s) con acuse logico; quedan 0.
```

## 4. Confirmación de la consola: el nodo entra con el relevo

Bitácora (`audit_logs`, UTC):

```text
15:41:39.274  ROTAR_CREDENCIAL_DISPOSITIVO   {"causa": "preventiva", ...}
15:41:39.379  BROKER_OPERACION_PENDIENTE     {"motivo": "modo_manual", "operacion": "aprovisionar"}
15:42:09.960  CONFIG_MODIFICADA              {"clave": "mqtt_relevo", "accion": "set"}
15:43:12.011  BROKER_CONSOLA_CONFIRMADA      {"operacion": "aprovisionar"}
15:43:12.038  DISPOSITIVO_OFFLINE            {"origen": "lwt", "boot_id": 687}
15:43:12.506  DISPOSITIVO_ONLINE             {"boot_id": 687, "pendientes": 0}
15:43:12.523  CREDENCIAL_ROTADA              {"boot_id": 687, "detalle": {"origen": "relevo"}}
```

La expulsión de la sesión al confirmar produce un LWT (`DISPOSITIVO_OFFLINE`), y el nodo vuelve en 0,5 s.

## 5. Rotación por compromiso: retención y entrega

Rotación a las 15:44:21 con `causa: compromiso` (`rotacion_por_compromiso = true`). El nodo sigue
conectado con el token anterior, pero ya no recibe acuse lógico:

```text
[CAP] boot=687 seq=64 up=630 t=2026-10-27T15:44:28Z tq=sntp ... q=1
[Tx] ERROR: PUBACK si, acuse logico no (boot=687 seq=64): se conserva en LittleFS.
[CAP] boot=687 seq=65 ... q=2
[CAP] boot=687 seq=66 ... q=3
...
[CAP] boot=687 seq=77 ... q=14
```

Durante la retención (≈ 3 min):

- Ninguna lectura persistida después de 15:44:18.
- Bitácora: 5 × `LECTURA_RETENIDA_ROTACION_COMPROMISO`, 1 × `EVENTO_RETENIDO_ROTACION_COMPROMISO`,
  1 × `TELEMETRIA_RETENIDA_ROTACION_COMPROMISO`. El nodo espacia los reintentos de drenaje (5 → 60 s), por
  lo que no se audita una fila por cada lectura capturada.
- `incidentes_operativos`: SIN_DATOS abierto a las 15:44:48 con umbral 30 s (3 × 10 s).

Relevo cargado (rechazado: CONNACK 4, el nodo sigue con el vigente), contraseña cambiada en el broker y
confirmación a las 15:47:37:

```text
15:47:37.443  BROKER_CONSOLA_CONFIRMADA
15:47:38.030  CREDENCIAL_ROTADA              {"origen": "relevo"}
15:47:38.155  SIN_DATOS_RESUELTO
```

Estado del nodo tras la entrega: `buffer: pendientes=0/30000`. En PostgreSQL, arranque 687 desde
`seq_no` 60: **28 lecturas, `seq_no` 60–87, 28 distintas** (sin huecos ni duplicados), con su hora de
captura original (15:43:48 a 15:48:18).

## 6. Particiones (ADR-18)

Primer `estado_nodo` tras flashear la tabla nueva (leído en `farmacias/ESP32_FARMACIA_01/eventos`):

```text
{'fs_total_bytes': 1507328, 'fs_libres_bytes': 1478656, 'capacidad': 30000, 'pendientes': 0, 'intervalo_s': 10}
```

1 507 328 B = 1472 KB (la partición `littlefs` de `partitions.csv`), por encima de los 1 310 720 B que
exige `core::bytesMinimosParticion`. El backend tomó el intervalo de `estado_nodo` (`devices.intervalo_reporte_segundos = 10`).

Ocupación de la partición de aplicación (1 310 720 B), compilando con la CA local de `infra-local`:

| Entorno | Usado | % |
|---|---:|---:|
| `esp32dev` | 1 164 872 B | 88,9 % |
| `demo` | 1 181 876 B | 90,2 % |
| `produccion` | 1 153 567 B | 88,0 % |

Antes de flashear se vaciaron las 222 lecturas pendientes con el firmware anterior (la tabla nueva borra
LittleFS); NVS no cambia de dirección (`0x9000`) y conservó Wi-Fi y credenciales.

## 7. Pendiente / fuera de alcance de esta evidencia
- Repetir el ciclo con la **consola real de EMQX Cloud Serverless** (DoD de HU-44).
- Revocación desde el backend en modo manual y el sondeo periódico del relevo cada 5 min (aquí el nodo
  probó el relevo en cuanto se cargó).
- Ensayo de saturación del buffer para confirmar que entran las 30 000 lecturas (ADR-18).
- Observación: durante la retención por compromiso el incidente SIN_DATOS se abre como un nodo caído; si el
  correo está habilitado, el aviso no distingue «telemetría retenida por rotación».
