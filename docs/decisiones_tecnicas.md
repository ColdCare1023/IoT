# Decisiones técnicas del firmware Edge (ADR resumidos)

Cada decisión indica el requisito que la motiva y lo que se sacrifica.

## ADR-01 · Buffer offline en registros binarios de 40 B, no en un JSON por archivo

- **Contexto.** HU-06 pide aguantar desconexiones prolongadas en la partición LittleFS de 1,6 MB.
  El firmware anterior guardaba un JSON (~500 B) por archivo con tope de 200 archivos (≈100 min a 30 s).
  En LittleFS cada archivo pequeño consume además metadatos propios.
- **Decisión.** Cada lectura se guarda como registro de 40 B con CRC32, en segmentos de 100 registros
  (4000 B ≈ 1 bloque). El JSON v1.0 se genera **al publicar**, de forma determinista desde el registro.
- **Consecuencias.** Capacidad configurada: 300 segmentos = **30 000 lecturas ≈ 10,4 días a 30 s**
  (a validar en el piloto, HU-06 esc. 4). Un registro dañado se detecta con certeza (CRC32) y va a
  cuarentena. `boot_id`, `seq_no`, `captured_at` y valores no pueden alterarse entre captura y envío.
  Costo: un cambio de contrato JSON no reescribe lo ya almacenado (se serializa con el formato vigente;
  `firmware_version` sí es la de captura porque va dentro del registro).

## ADR-02 · Liberar solo con acuse lógico; cuarentena en vez de borrar

- **Contexto.** Regla de oro: el `PUBACK` solo confirma recepción en el broker, no el `COMMIT`.
- **Decisión.** Un registro solo se libera con `committed`/`duplicate` del backend para el mismo
  `(boot_id, seq_no)`. `rejected`, CRC inválido o payload inválido → copia íntegra a
  `/tt/cuarentena.bin` con su motivo, y el FIFO continúa.
- **Consecuencias.** Un registro malo no bloquea para siempre la cola, y nada desaparece sin copia.
  La cuarentena tiene tope (250 registros); si se desborda se cuenta y se reporta.

## ADR-03 · Saturación: preservar lo antiguo, contar lo nuevo

- **Contexto.** Doc. de responsabilidades: "preserva los registros antiguos sin sobrescribirlos
  silenciosamente e incrementa un contador persistente". El firmware anterior hacía lo contrario
  (borraba el más antiguo).
- **Decisión.** Al 100 % la lectura nueva no se guarda; se incrementa `perdidas_total` en **NVS**
  (no en LittleFS, que está lleno) con el periodo `(boot_id, seq_no, época)` inicial y final del
  episodio. Aviso único al cruzar el 80 % (rearme bajo 70 %).
- **Consecuencias.** La brecha es visible por tres vías: evento `buffer_saturado`, salto de `seq_no`
  y contador en `estado_nodo` (HU-48 esc. 2).

## ADR-04 · Sin hora inventada

- **Contexto.** El firmware anterior, sin NTP, usaba la hora de compilación: fechas plausibles pero falsas.
- **Decisión.** `captured_at = null` + `time_quality = "unsynced"` si no hay SNTP. Si SNTP llega más tarde
  en el mismo arranque, se retrofecha con el uptime monótono (`sntp_backfilled`). `uptime_s` viaja siempre.

## ADR-05 · Drivers propios para DS18B20 y SHT31

- **Contexto.** El requisito pide CRC explícito, timeout y motivo de falla. DallasTemperature devuelve
  −127 °C para todo error; Adafruit_SHT31 devuelve NaN sin distinguir CRC de ausencia.
- **Decisión.** OneWire directo (scratchpad + CRC-8 Maxim) y Wire directo (trama de 6 B + CRC-8 Sensirion).
  La decodificación está en `core/Sensores.cpp`, probada con los vectores de las hojas de datos.
- **Detalle notable.** Un scratchpad de nueve ceros tiene CRC 0 y "pasaría" el checksum como 0,0 °C:
  se rechaza antes. El valor 85,0 °C de *power-on reset* se marca `power_on_reset`.

## ADR-06 · MQTT 3.1.1 (desviación documentada respecto a "MQTT 5.0")

- **Contexto.** El documento pide MQTT 5.0. El core Arduino fijado (2.0.17 / ESP-IDF 4.4) no incluye
  cliente MQTT 5, y la librería con PUBACK verificable (`256dpi/MQTT`, lwmqtt) es 3.1.1.
- **Decisión.** Mantener 3.1.1 sobre TLS 8883. EMQX Serverless acepta ambos.
- **Qué se cumple igual.** TLS con validación de CA y hostname, sin fallback; QoS 1 con PUBACK; LWT;
  usuario = `device_id` + token; ACL por tópico en el broker; acuse lógico de aplicación.
- **Qué cambia.** El rechazo de credenciales llega como código 3.1.1 `0x05 Not authorized` en vez de
  `0x87` (HU-10/HU-44 lo piden "cuando aplique"). Sin *reason strings* ni *user properties*.
- **Salida futura.** Migrar a Arduino core 3.x (ESP-IDF 5.1+, `esp-mqtt` con `CONFIG_MQTT_PROTOCOL_5`),
  reescribiendo `system/Watchdog.cpp` para la API de IDF 5 y revalidando todo en hardware.

## ADR-07 · Sin lectura ADC de batería (GPIO 34 eliminado)

- **Contexto.** El Mini UPS Forza 5 V no expone nivel de batería. El pin quedaba flotante: picos y
  lecturas falsas.
- **Decisión.** No hay lectura analógica. La continuidad se evidencia por la secuencia ininterrumpida
  `boot_id`/`seq_no` durante el corte (lo verifica el analizador de ensayos de QA a partir de las líneas `[BOOT]`/`[CAP]`).
  Opcional: entrada **digital** GPIO 27 que ve el 5 V del adaptador **antes** del UPS →
  `power_source` y eventos de conmutación; el aviso de autonomía crítica es una **estimación** a partir
  de la autonomía medida en el ensayo C-6.

## ADR-08 · Excursiones simuladas solo en un firmware aparte

- **Contexto.** La demo necesita provocar una excursión sin arriesgar medicamentos.
- **Decisión.** Entorno `demo` (`MODO_DEMO=1`, `device_id = ESP32_DEMO_01`): botón BOOT 1 s o comando
  `demo excursion 12.5 4`. Cada lectura afectada lleva `"simulated": true`. El código no existe en el
  binario de producción (`#if MODO_DEMO`). Alternativa sin hardware: `simulacion_excursiones/` en el repositorio ColdCare1023/Backend.

## ADR-09 · Dos tareas en el núcleo 1, captura con más prioridad

- Captura (prioridad 3) y red (2) en el núcleo 1; la pila Wi-Fi queda en el 0. La captura preempta el
  handshake TLS y **nunca** espera a la red: el mutex del buffer solo se toma por operación de cola y se
  suelta durante el PUBACK + acuse (verificado por `test_el_cerrojo_no_se_sostiene_durante_el_envio`).
- Watchdog = 2 × (TLS 10 s + CONNACK 5 s) = 30 s ≤ keep-alive 60 s (`static_assert`).

## ADR-10 · El puerto y el esquema MQTT no son configurables (HU-14)

- `MQTT_PORT` fijo en 8883: definirlo por build flag es un `#error`, y un `static_assert` en el cliente
  impide otro valor. `mqtt_host` solo acepta un nombre de host (sin `mqtt://`, sin `:1883`, sin ruta).
  Una configuración errónea deja al nodo sin conectar (y guardando en LittleFS), nunca en texto plano.
