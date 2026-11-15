# Firmware v1.2.1: validación en hardware pendiente (H1 y brechas de la validación final)

Rama `fix/firmware-r2` (sobre `develop` 1685b36). Cubre el P0-5 de `VALIDACION_FINAL_BACKLOG.md`
(H1: HU-08 esc. 3, HU-06, HU-09 esc. 3) y los P1-4 (HU-48), P1-5 (HU-04) y los P2 de HU-02 y HU-13.

**Estado:** verificado solo en software. Ningún ensayo de este documento se ha ejecutado todavía en el
ESP32. Mientras no se ejecuten, la corrección de H1 sigue sin evidencia en el nodo.

## 1. Qué cambió y qué ya está verificado sin hardware

| Cambio | Verificado en software | Falta en hardware |
|---|---|---|
| H1: `Stream::setTimeout()` recibe ms (`core::PLAZO_CLIENTE_TLS_MS` = 10 000) y `setHandshakeTimeout()` s (`PLAZO_HANDSHAKE_TLS_S` = 10). Firmas comprobadas en framework-arduinoespressif32 3.3.12 | `test_plazos_conexion` (6), incluida una revisión del código fuente de `MQTTManager.cpp` que falla con la versión de `develop` | Ensayos 1 y 2 |
| Watchdog de 60 s = peor `connect()` (DNS 21 s + TCP 10 s + TLS 10 s + lectura 10 s + CONNACK 5 s) + 4 s, ≤ keep-alive. `conectar()` alimenta el watchdog entre CONNECT y SUBACK | `static_assert` + `test_watchdog_cubre_el_peor_intento_y_no_pasa_el_keepalive` | Ensayos 1 y 2 (cero `TASK_WDT`) |
| Episodio de reconexión persistente en NVS (intentos, desde cuándo, reinicios, etapa) en `estado_nodo` y `wifi_reconexion_prolongada` | `test_reconexion` (9): sobrevive a un reinicio, persistencia acotada, reporte | Ensayo 3 |
| `estado_nodo.pendientes_desde/hasta` (HU-48) | `test_estado_nodo` (8) y 3 pruebas de extremos en `test_cola` | Ensayo 5 |
| Cambios de puerta en LittleFS hasta su PUBACK, con `occurred_at` del cambio (HU-04) | `test_bitacora_puerta` (7) | Ensayo 4 |
| SHT31: tras 3 fallas de comunicación seguidas, 9 pulsos de SCL + STOP y `begin()` (HU-02) | `test_bus_i2c` (6) | Ensayo 6 |
| `desconexion_ordenada` antes de un DISCONNECT voluntario (HU-13 esc. 2) | `test_desconexion_ordenada_lleva_el_motivo` | Ensayo 7 (requiere el cambio del backend) |

Compilación: `pio run -e esp32dev` y `-e demo` sin warnings propios (Flash 91,7 %). `produccion` no se
compiló: exige `data/certs/root_ca.pem` (fuera del repositorio a propósito) y la recompilación híbrida
de ESP-IDF.

## 2. Preparación

1. Compilar desde `fix/firmware-r2` el mismo entorno que usa hoy el nodo y grabarlo en
   **ESP32_FARMACIA_01**: `pio run -e esp32dev -t upload` (no ejecutar `uploadfs`: borra el buffer).
2. Abrir el monitor serie con hora (`pio device monitor`; el filtro `time` ya está en platformio.ini) y
   guardar el log completo de cada ensayo (`log2file`).
3. Comprobar en la línea `[BOOT]` `fw=v1.2.1` y anotar `boot_id`.
4. Anotar en la evidencia la hora de inicio y fin de cada ensayo: mientras dura, el backend abre
   «Sin datos» (HU-54). Es esperado.

## 3. Ensayos

### Ensayo 1 · Broker inalcanzable ≥ 2 h (H1, HU-08 esc. 3, HU-09 esc. 3)

Dos variantes, cada una ≥ 2 h con el AP encendido y con internet:

- **1a, DNS que no resuelve:** `nvs set mqtt_host noexiste.invalid` y reiniciar. Al terminar, restaurar
  el host real y reiniciar.
- **1b, TCP sin respuesta:** en el router, descartar (DROP, no REJECT) el tráfico saliente a TCP 8883 del
  host de EMQX. Al terminar, quitar la regla.

Criterios de aceptación:

- Ninguna línea `[BOOT] ... reset=TASK_WDT` en todo el ensayo (antes: un reinicio cada 34–140 s).
- `[CAP]` en cada intervalo, `seq` sin huecos y `g=OK`: la captura offline no se detiene.
- `[MQTT] ERROR: Conexion rechazada` espaciados por el backoff (5 s → 60 s), no en ráfaga.
- Tras el umbral: `[Red] ERROR: DIAGNOSTICO: N intentos de reconexion fallidos (ultimo: broker)`.
- `estado` en la consola: `reconexion: EN_CURSO intentos=N etapa=broker reinicios=0`.
- Al restaurar: evento `wifi_reconexion_prolongada` con `etapa = broker`, `reinicios = 0`,
  `desde`/`hasta` ≈ inicio y fin del ensayo; el buffer se drena (HU-07) y el primer `estado_nodo` trae
  `reconexion_*` con los mismos valores.

### Ensayo 2 · AP apagado ≥ 2 h (HU-08 esc. 1–3, HU-06)

Apagar el router con el nodo conectado. Criterios: los mismos del ensayo 1 con `etapa = wifi`; backoff
Wi-Fi 1 → 60 s; al encender el AP, reconexión sin intervención, evento con `intentos_fallidos` y
`duracion_s` ≈ duración del corte, y drenaje completo (`pendientes=0`).

### Ensayo 3 · Reinicio con el episodio abierto (HU-08 esc. 3, "se registra el estado localmente")

Durante el ensayo 1 o 2, pulsar EN (o cortar la alimentación 5 s). Criterios:

- Tras el arranque, `estado` muestra el **mismo** `desde=boot X +Ys` de antes del reinicio y
  `reinicios=1`; `intentos` no vuelve a 0.
- Al restaurar la red, `wifi_reconexion_prolongada.reinicios = 1` y el `arranque` de ese boot lleva el
  `reset_reason` del reinicio.

### Ensayo 4 · Puerta sin red y reinicio (HU-04 esc. 1, HU-35)

Con el broker inalcanzable (variante 1b): abrir y cerrar la puerta 3 veces anotando la hora de cada
`[MC38] Puerta ABIERTA/cerrada`; `estado` debe mostrar `puerta: pendientes=6`. Reiniciar el nodo
(botón EN o `reiniciar`), comprobar `[Puerta] 6 cambio(s) de puerta sin publicar recuperados.` y restaurar el
broker. Criterios: llegan 6 eventos `puerta` en orden, con `occurred_at` igual (±1 s) a la hora de cada
cambio en el log serie y `boot_id` del arranque en que ocurrieron; `puerta_pendientes` vuelve a 0.

### Ensayo 5 · Rango real de pendientes en el tablero (HU-48 esc. 1 y 3)

Con el nodo de banco (`ESP32_BANCO_01`, ADR-18) y el broker caído, `buffer llenar 30000`; al restaurar,
el drenaje dura varios latidos. Criterios: cada `estado_nodo` trae `pendientes_desde` y
`pendientes_hasta`; el tablero muestra el rango **sin** la marca de estimado (`rango_estimado = false`);
al terminar, `pendientes = 0` y el indicador desaparece.

### Ensayo 6 · Recuperación del SHT31 (HU-02)

Con el nodo capturando, desconectar SDA del SHT31 (o puentearla a GND) durante 3 intervalos y volver a
conectarla. Criterios: `[SHT31] ERROR: 3 mediciones seguidas sin comunicacion: bus liberado con N
pulso(s) de SCL ...`; la medición siguiente vuelve a `sht31_status = OK` **sin** reiniciar el nodo (mismo
`boot_id`); `estado_nodo.sht31_recuperaciones` aumenta; el DS18B20 no se ve afectado.

### Ensayo 7 · Desconexión ordenada (HU-13 esc. 2)

Requiere que el backend acepte `desconexion_ordenada` en `contrato_v1.EventoV1` (pendiente en el repo
Backend). Cargar un token de relevo (`nvs set mqtt_relevo ...`). Criterios: el nodo publica
`desconexion_ordenada` con `motivo = prueba_relevo` antes del DISCONNECT, EMQX no publica LWT y el
tablero no marca el equipo como caído.

## 4. Fuera del alcance de este documento

Consola de EMQX, panel de Railway y el despliegue del backend. El cambio de `EventoV1` para
`desconexion_ordenada` va en el repositorio Backend.
