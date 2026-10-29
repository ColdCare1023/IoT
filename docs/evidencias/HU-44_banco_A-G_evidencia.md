# Evidencia de QA y Validación — HU-44: pruebas de banco A a G (rotación, prueba de posesión, revocación)

## 1. Resumen
- **ID**: HU-44 (rotación y revocación de credenciales del nodo) · ADR-17 (token de relevo) · HU-10 (escenarios 2 y 3)
- **Fecha de ejecución**: 28/10/2026, 23:24–23:38 UTC
- **Equipo bajo prueba**: ESP32 DevKitC V4 en protoboard (`ESP32_FARMACIA_01`), firmware `esp32dev` en
  `0fb14e8` (`develop`), `intervalo_s = 10` (banco), DS18B20, SHT31 y MC-38 conectados. Arranque `boot_id` 697.
- **Entorno**: pila local `Backend/infra-local` con el backend en `62082c9` (`develop`), EMQX 5.8.6 con
  autenticación integrada y TLS 8883, PostgreSQL 16. Backend con `BROKER_USUARIOS_MODO=manual` (modo de EMQX
  Cloud Serverless) mediante un archivo de sobrescritura temporal de Compose.
- **Herramientas**: `Backend/scripts/pruebas_hu44_banco.py` (subcomandos `atacante`, `token-invalido`, `acl`,
  `confirmar`, `revocar`, `informe`) y la consola serie del nodo.
- **Limitación**: el paso «cambiar la contraseña en la consola de EMQX» se hizo con la API de
  administración del EMQX local (`PUT` y `DELETE /authentication/password_based:built_in_database/users/{id}`),
  que tiene el mismo efecto sobre el broker. **No es la consola de EMQX Cloud Serverless (prueba H).**
- Los tokens se cargaron por la consola serie sin mostrarse («valor no mostrado»); en los logs adjuntos
  aparecen como `<token>`.
- **Anexos** (en el repo Backend, `docs/evidencias/logs/`): log serie `2026-10-28_hu44_banco_serie.log` (B' a G) y bitácora en CSV
  `2026-10-28_hu44_banco_bitacora.csv` (`pruebas_hu44_banco.py informe`).

Estas pruebas repiten A y B (27/10, `HU-44_rotacion_relevo_evidencia.md`) con los cambios de backend
`252dfc2` y de firmware `1aea6bd`/`83d8f78`: la retención por compromiso ya no termina al confirmar la
consola, sino cuando el nodo supera la prueba de posesión.

---

## 2. Checklist de Verificación y Resultados

| Prueba | Criterio verificado | Estado | Evidencia |
|---|---|---|---|
| **A. Rotación preventiva** | Relevo rechazado (CONNACK 4) mientras la consola está pendiente; al confirmar, el nodo entra con el relevo y publica `credencial_rotada` con `origen: relevo`; reconexiones etiquetadas con su motivo. | **PASS** (0,45 s sin conexión) | Sección 3 |
| **B'. Rotación por compromiso** | Retención sin acuse hasta la prueba de posesión; `monitoreo_suspendido` en lugar de SIN_DATOS; entrega de lo retenido con su hora de captura. | **PASS** (19/19 lecturas; prueba 8,6 s después de confirmar) | Sección 4 |
| **C. Excursión durante la retención** | `excursion_no_verificada` abierta con la lectura retenida; un solo episodio por ventana; se resuelve al verificar. | **PASS** (`captura_a_alerta_s = 1,0`) · correo no probado | Sección 5 |
| **D. Atacante con el token viejo** | En la ventana: nada se persiste ni se acusa. Tras cambiar la consola: rechazo por credencial. | **PASS** | Sección 6 |
| **E. Confirmar sin cambiar la consola** | `PRUEBA_POSESION_FALLIDA` (`firma_invalida`), la retención sigue, intentos limitados; tras cambiar la consola, «Reenviar desafío» verifica. | **PASS** (14/14 lecturas) | Sección 7 |
| **F. Revocación** | El token revocado se rechaza en el broker; la telemetría recibe acuse `rejected`. | **PASS con defecto**: el nodo no recibe el acuse `rejected` | Sección 8 |
| **G. Token inválido y ACL** | Escenarios 2 y 3 de HU-10: `bad_username_or_password` y `authorization_permission_denied`; nada registrado del nodo ajeno. | **PASS** | Sección 9 |
| **Continuidad del arranque 697** | Todas las lecturas del arranque llegan a PostgreSQL sin huecos ni duplicados tras A–G. | **PASS** (90/90, `seq_no` 1–90) | Sección 10 |

---

## 3. Prueba A — rotación preventiva

`POST /credenciales/rotar` con `causa: preventiva` (23:25:00) responde
`{"credencial_aprovisionada": null, "sesion_broker_cerrada": true, "telemetria_retenida": false}`. Al cargar
el relevo con la consola de EMQX aún pendiente:

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

Contraseña cambiada en el broker (HTTP 200) y confirmación (`posesion_pendiente: false`). Bitácora (UTC):

```text
23:25:00.438  ROTAR_CREDENCIAL_DISPOSITIVO   {"causa": "preventiva"}
23:25:00.475  BROKER_OPERACION_PENDIENTE     {"motivo": "modo_manual", "operacion": "aprovisionar"}
23:25:00.479  DISPOSITIVO_OFFLINE            {"motivo": "reconexion_por_rotacion", "origen": "lwt"}
23:25:00.912  DISPOSITIVO_ONLINE             {"motivo": "reconexion_por_rotacion"}
23:25:02.106  CONFIG_MODIFICADA              {"clave": "mqtt_relevo", "accion": "set"}
23:25:49.283  BROKER_CONSOLA_CONFIRMADA      {"operacion": "aprovisionar"}
23:25:49.315  DISPOSITIVO_OFFLINE            {"motivo": "reconexion_por_confirmacion_consola", "origen": "lwt"}
23:25:49.763  DISPOSITIVO_ONLINE             {"motivo": "reconexion_por_confirmacion_consola", "pendientes": 0}
23:25:49.776  CREDENCIAL_ROTADA              {"detalle": {"origen": "relevo"}}
```

El nodo vuelve 0,45 s después de la expulsión y no queda ninguna lectura pendiente.

## 4. Prueba B' — rotación por compromiso con prueba de posesión

Rotación a las 23:27:29 con `causa: compromiso` (`telemetria_retenida: true`). El backend expulsa la sesión,
el nodo vuelve con el token vigente y deja de recibir acuse lógico:

```text
23:27:34.628 >> nvs set mqtt_relevo @relevo
23:27:40.046 [Tx] ERROR: PUBACK si, acuse logico no (boot=697 seq=25): se conserva en LittleFS.
23:27:40.454 [Red] Broker rechazo el token de relevo (CONNACK 4): se sigue con el vigente (HU-44).
23:27:49.018 [Tx] ERROR: PUBACK si, acuse logico no (boot=697 seq=25): se conserva en LittleFS.
...
23:30:16.873 [Tx] ERROR: PUBACK si, acuse logico no (boot=697 seq=25): se conserva en LittleFS.
```

Durante la ventana (≈ 3 min) no se persistió ninguna lectura. Contraseña cambiada y confirmación a las
23:30:22.489:

```text
23:30:22.623 [MQTT] Conectando a 192.168.18.21:8883 como 'ESP32_FARMACIA_01'...
23:30:23.032 [MQTT] Conectado (TLS, QoS 1). Acuses en farmacias/ESP32_FARMACIA_01/ack.
23:30:23.032 [Evento] Encolado 'credencial_rotada'.
23:30:31.241 [Tx] ERROR: PUBACK si, acuse logico no (boot=697 seq=25): se conserva en LittleFS.
23:30:31.241 [MQTT] Desafio de posesion respondido.
23:30:34.925 [Red] 19 lectura(s) con acuse logico; quedan 0.
```

Bitácora:

```text
23:27:29.320  ROTAR_CREDENCIAL_DISPOSITIVO              {"causa": "compromiso"}
23:27:29.367  TELEMETRIA_RETENIDA_ROTACION_COMPROMISO
23:27:29.780  EVENTO_RETENIDO_ROTACION_COMPROMISO       {"tipo_evento": "online", "suprimidos_desde_anterior": 0}
23:27:31.964  LECTURA_RETENIDA_ROTACION_COMPROMISO      {"suprimidos_desde_anterior": 0}
23:28:41.096  RETENCION_INTENTOS_SUPRIMIDOS             {"cierre": "ventana_vencida", "suprimidos_sin_auditar": {"evento_retenido": 2, "lectura_retenida": 4}}
23:29:41.156  RETENCION_INTENTOS_SUPRIMIDOS             {"cierre": "ventana_vencida", "suprimidos_sin_auditar": {"evento_retenido": 2, "lectura_retenida": 5}}
23:30:22.489  BROKER_CONSOLA_CONFIRMADA                 {"operacion": "aprovisionar"}
23:30:31.115  CREDENCIAL_POSESION_VERIFICADA            {"motivo": "verificada", "boot_id": 697}
23:30:31.126  MONITOREO_SUSPENDIDO_RESUELTO             {"duracion_s": 181.7}
23:30:31.153  RETENCION_INTENTOS_SUPRIMIDOS             {"cierre": "verificada", "suprimidos_sin_auditar": {"evento_retenido": 3, "lectura_retenida": 2}}
23:30:31.271  DISPOSITIVO_ONLINE                        {"origen": "telemetria", "seq_no": 25}
```

- **Tiempo de confirmación a prueba verificada: 8,6 s.** El desafío que el backend publica al confirmar
  llega antes de que el nodo se suscriba a `.../ack` y se pierde. El nodo espera ≈ 8 s el acuse lógico de
  la cabeza retenida (`seq_no` 25) y solo entonces atiende el desafío que el backend reemite a la sesión
  nueva. Ver observación 2 de la sección 11.
- `incidentes_operativos`: `monitoreo_suspendido` abierto a las 23:27:29 y resuelto a las 23:30:31; **no se
  abrió SIN_DATOS** durante la retención.
- El limitador de auditoría dejó una fila por clave y minuto; lo suprimido se volcó con `cierre:
  ventana_vencida` y, al verificar, con `cierre: verificada`.
- PostgreSQL: **19 lecturas, `seq_no` 25–43, 19 distintas**, con su hora de captura original (23:27:31 a
  23:30:31), recibidas entre 23:30:31 y 23:30:34.

## 5. Prueba C — excursión durante la retención

Con el nodo a temperatura ambiente (≈ 26,4 °C, fuera de 2–8 °C), la primera lectura retenida abre el
incidente sin esperar a la verificación:

```text
23:27:31.982  EXCURSION_NO_VERIFICADA_DETECTADA  {"origen": "lectura_retenida", "seq_no": 25, "boot_id": 697,
              "captured_at": "2026-10-28T23:27:31+00:00", "origen_verificado": false,
              "captura_a_alerta_s": 1.0, "temperatura_interna": 26.38}
23:30:31.133  EXCURSION_NO_VERIFICADA_RESUELTO   {"duracion_s": 179.1}
```

- Un solo incidente en toda la ventana. Su `detalle` se actualizó con la última captura retenida
  (`ultima_captura: 23:30:21`, `ultima_temperatura_interna: 26.38`, `ultima_en_rango: false`).
- El mismo comportamiento se repitió en la ventana de la prueba E (abierto 23:31:42, `seq_no` 50,
  `captura_a_alerta_s = 1,0`, resuelto 23:33:54).
- **No probado**: el correo del incidente (el SMTP no está configurado en la pila local).

## 6. Prueba D — atacante con el token viejo

**Durante la ventana** (23:28:49), `pruebas_hu44_banco.py atacante` con el token anterior y el mismo
client id que el nodo:

```text
23:28:49.725  [D] CONNACK 0: el broker aceptó la conexión como ESP32_FARMACIA_01 (client id ESP32_FARMACIA_01).
23:28:49.728  [D] PUBACK recibido para farmacias/ESP32_FARMACIA_01/lecturas. Espero acuse lógico 15 s…
23:28:50.502  [D] Error MQTT: Disconnected during message iteration
```

El nodo, expulsado por el mismo client id, reconecta en 0,8 s (`23:28:50.612 [MQTT] Conectado`) y a su vez
expulsa al atacante (expulsión mutua). El atacante no recibió acuse lógico y **ninguna lectura suya se
persistió** (0 filas con su temperatura de 4,0 °C).

**Tras cambiar la consola** (23:31:24):

```text
23:31:24.097  [D] CONNACK 134: rechazo por credencial (esperado) — [code:134] Bad user name or password
```

134 es el código MQTT 5 equivalente al CONNACK 4 de MQTT 3.1.1 que usa el nodo.

## 7. Prueba E — confirmar sin cambiar la consola

Rotación por compromiso a las 23:31:41 y confirmación a las 23:32:16 **sin** cambiar la contraseña en el
broker. El nodo vuelve con el token viejo y firma el desafío con él:

```text
23:32:16.779  BROKER_CONSOLA_CONFIRMADA
23:32:25.769  PRUEBA_POSESION_FALLIDA     {"motivo": "firma_invalida", "boot_id": 697, "suprimidos_desde_anterior": 0}
23:32:55.860  LECTURA_RETENIDA_ROTACION_COMPROMISO {"suprimidos_desde_anterior": 6}
23:33:11.338  RETENCION_INTENTOS_SUPRIMIDOS {"cierre": "ventana_vencida", "suprimidos_sin_auditar": {"evento_retenido": 4}}
23:33:32.004  PRUEBA_POSESION_FALLIDA     {"motivo": "firma_invalida", "suprimidos_desde_anterior": 1}
```

El script `confirmar` informó «Sin prueba de posesión verificada en 60 s: la retención sigue». El nodo
respondió el desafío con el token viejo 3 veces (23:32:25, 23:32:52 y 23:33:32) y la bitácora dejó una
sola fila `PRUEBA_POSESION_FALLIDA` por minuto.

Contraseña cambiada en el broker y «Reenviar desafío» (`POST /credenciales/reenviar-desafio`) a las
23:33:45:

```text
23:33:45.512  DESAFIO_POSESION_REENVIADO
23:33:54.103  CREDENCIAL_POSESION_VERIFICADA  {"motivo": "verificada", "boot_id": 697}
23:33:54.121  RETENCION_INTENTOS_SUPRIMIDOS   {"cierre": "verificada", "suprimidos_sin_auditar": {"evento_retenido": 3, "lectura_retenida": 2}}
```

Verificada 8,6 s después del reenvío. PostgreSQL: **14 lecturas, `seq_no` 50–63**, con su hora de captura
(23:31:41 a 23:33:51). `monitoreo_suspendido` abierto 23:31:41, resuelto 23:33:54.

## 8. Prueba F — revocación

`pruebas_hu44_banco.py revocar` a las 23:34:43 (HTTP 204). La publicación del script con el token revocado
(client id `ESP32_FARMACIA_01-banco`) recibe el acuse esperado:

```text
23:34:43.432  [F] Mensaje en .../ack: {"device_id": "ESP32_FARMACIA_01", "estado": "dispositivo_no_autorizado", "status": "rejected", ...}
```

El nodo, en cambio, **no recibe el acuse `rejected`**: el backend expulsa su sesión antes de publicarlo
(`Backend/src/interface/main.py`, rama `DispositivoNoAutorizadoError`). El nodo reconecta cada ≈ 5 s, el
broker lo sigue aceptando (en modo manual el usuario existe hasta borrarlo en la consola) y repite el ciclo:

```text
23:34:54.513 [MQTT] ERROR: Sin PUBACK (lastError=-5).
23:34:54.513 [Red] ERROR: El broker no confirmo el evento 'lectura_vigente'.
23:35:00.049 [Tx] ERROR: PUBACK si, acuse logico no (boot=697 seq=69): se conserva en LittleFS.
...
23:35:09.174 reloj=sntp_synced | buffer: pendientes=2/30000 ocupacion=0% libres=1444KB cuarentena=0
```

- Bitácora entre 23:34:43 y 23:36:06 (83 s): **35 `EVENTO_CREDENCIAL_REVOCADA`** y **10
  `LECTURA_RECHAZADA_CREDENCIAL_REVOCADA`** (1 del script y 9 del nodo). Estas filas no pasan por el
  limitador de auditoría.
- `cuarentena=0`: como el acuse `rejected` no llega, las lecturas se conservan como pendientes en lugar de
  pasar a cuarentena.
- Usuario borrado en el broker y confirmación (`operacion: retirar`) a las 23:36:11: desde entonces el
  broker rechaza al nodo con **CONNACK 5** (no autorizado) y cesan las filas.
- SIN_DATOS abierto 23:35:12 y resuelto 23:37:17, al restaurar el nodo.

**Restauración**: backend de vuelta a modo `api`, credencial nueva (23:37:09) cargada con
`nvs set mqtt_token` (23:37:16). El nodo conecta en 0,2 s y entrega las 15 lecturas pendientes
(`seq_no` 69–83, capturadas entre 23:34:51 y 23:37:11).

## 9. Prueba G — token inválido y ACL

`pruebas_hu44_banco.py token-invalido` y `acl --otro FARM-02-TEST` a las 23:34:25:

```text
23:34:25.113  [G-token] CONNACK 134: rechazo por credencial (esperado) — [code:134] Bad user name or password
23:34:25.548  [G-acl] Conectado como ESP32_FARMACIA_01. Publico en farmacias/FARM-02-TEST/lecturas…
```

`docker compose logs emqx`:

```text
23:34:25.111 [warning] tag: AUTHN, clientid: ESP32_FARMACIA_01-banco, msg: authentication_failure, username: ESP32_FARMACIA_01, reason: bad_username_or_password
23:34:25.549 [info]    tag: AUTHZ, clientid: ESP32_FARMACIA_01-banco, msg: authorization_permission_denied, username: ESP32_FARMACIA_01, topic: farmacias/FARM-02-TEST/lecturas, source: file, action: PUBLISH(Q1,R0)
23:34:25.550 [warning] tag: AUTHZ, msg: cannot_publish_to_topic_due_to_not_authorized, topic: farmacias/FARM-02-TEST/lecturas
```

PostgreSQL: **0 lecturas de `FARM-02-TEST`** desde las 23:20.

## 10. Continuidad del arranque 697

Tras A–G, `ESP32_FARMACIA_01` arranque 697: **90 lecturas, `seq_no` 1–90, 90 distintas**. Ninguna lectura
se perdió en las rotaciones, la revocación ni la restauración.

```sql
SELECT count(*), min(seq_no), max(seq_no), count(DISTINCT seq_no)
FROM thermal_readings WHERE device_id = 'ESP32_FARMACIA_01' AND boot_id = 697;
-- 90 | 1 | 90 | 90
```

## 11. Defectos y observaciones

1. **Backend — revocación (F).** En la rama `DispositivoNoAutorizadoError` de `main.py`,
   `_expulsar_sesion_revocada` se ejecuta antes de `_publicar_ack_lectura(..., estado="dispositivo_no_autorizado")`.
   El nodo no recibe el `rejected`, no pone nada en cuarentena y entra en un bucle de reconexión cada 5 s.
   Corrección: publicar primero el acuse, expulsar una sola vez y limitar `EVENTO_CREDENCIAL_REVOCADA` y
   `LECTURA_RECHAZADA_CREDENCIAL_REVOCADA` con `LIMITE_AUDITORIA`.
2. **Firmware — prueba de posesión (B', E).** El nodo atiende el desafío solo después de esperar el acuse
   lógico de la cabeza retenida (≈ 8 s). Atender el desafío durante esa espera bajaría el tiempo a < 1 s.
3. **Script de banco.** `pruebas_hu44_banco.py` necesitó cuatro arreglos locales: login como formulario
   OAuth2 (envía JSON y recibe 422), `WindowsSelectorEventLoopPolicy` para aiomqtt en Windows, salida con
   `PYTHONIOENCODING=utf-8` («→», «…») y `confirmar` repite las filas de auditoría en cada consulta.
4. **Documentación.** Durante la retención por compromiso el par LWT/`online` también se retiene, así que
   los motivos `reconexion_por_confirmacion_consola` y `reconexion_por_reenvio_desafio` no aparecen en B'
   ni en E (sí en A).
5. **Frontend.** Durante la excursión no verificada se muestra `detalle.temperatura_interna` (la lectura
   que abrió el incidente) y no `ultima_temperatura_interna`/`ultima_captura`.

## 12. Pendiente / fuera de alcance de esta evidencia
- **Prueba H**: repetir A–G con la **consola real de EMQX Cloud Serverless** (DoD de HU-44).
- Repetir F cuando se corrija el defecto 1: el nodo debe recibir `rejected` y poner las lecturas en cuarentena.
- Correo de `excursion_no_verificada` y de `monitoreo_suspendido` con SMTP real.
- Ensayo de saturación del buffer (ADR-18): ejecutado el 28–29/10 con `ESP32_BANCO_01`; saturó en 29 909 y no
  en 30 000, por lo que se repetirá tras corregir la liberación del segmento de cabeza.
