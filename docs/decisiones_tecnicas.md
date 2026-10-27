# Decisiones técnicas del firmware Edge (ADR resumidos)

Cada decisión indica el requisito que la motiva y lo que se sacrifica.

## ADR-01 · Buffer offline en registros binarios de 40 B, no en un JSON por archivo

- **Contexto.** HU-06 pide aguantar desconexiones prolongadas en la partición LittleFS (1,6 MB al decidirlo;
  1472 KB desde ADR-18).
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

- **Contexto.** El documento pide MQTT 5.0. El core Arduino fijado no incluye cliente MQTT 5 compilado
  (esp-mqtt sin `CONFIG_MQTT_PROTOCOL_5`), y la librería con PUBACK verificable (`256dpi/MQTT`, lwmqtt)
  es 3.1.1.
- **Decisión.** Mantener 3.1.1 sobre TLS 8883. EMQX Serverless acepta ambos.
- **Qué se cumple igual.** TLS con validación de CA y hostname, sin fallback; QoS 1 con PUBACK; LWT;
  usuario = `device_id` + token; ACL por tópico en el broker; acuse lógico de aplicación.
- **Qué cambia.** El rechazo de credenciales llega como código 3.1.1 `0x05 Not authorized` en vez de
  `0x87` (HU-10/HU-44 lo piden "cuando aplique"). Sin *reason strings* ni *user properties*.
- **Salida futura.** Con el core ya en 3.x (ADR-11), pasar a `esp-mqtt` con `CONFIG_MQTT_PROTOCOL_5`
  exige compilar el framework con `custom_sdkconfig` y revalidar el contrato de acuses en hardware.

## ADR-07 · Sin lectura ADC de batería (GPIO 34 eliminado)

- **Contexto.** El Mini UPS Forza 5 V no expone nivel de batería. El pin quedaba flotante: picos y
  lecturas falsas.
- **Decisión.** No hay lectura analógica. La continuidad se evidencia por la secuencia ininterrumpida
  `boot_id`/`seq_no` durante el corte (lo verifica el analizador de ensayos de QA a partir de las líneas `[BOOT]`/`[CAP]`).
  Opcional: entrada **digital** GPIO 26 que ve el 5 V del adaptador **antes** del UPS →
  `power_source` y eventos de conmutación; el aviso de autonomía crítica es una **estimación** a partir
  de la autonomía medida en el ensayo C-6.

## ADR-08 · Excursiones simuladas solo en un firmware aparte

- **Contexto.** La demo necesita provocar una excursión sin arriesgar medicamentos.
- **Decisión.** Entorno `demo` (`MODO_DEMO=1`, `device_id = ESP32_DEMO_01`): botón BOOT 1 s o comando
  `demo excursion 12.5 4`. Cada lectura afectada lleva `"simulated": true`. El código no existe en el
  binario de producción (`#if MODO_DEMO`). Alternativa sin hardware: `simulacion_excursiones/` en el repositorio ColdCare1023/Backend.
- **IOT-005.** El firmware demo nunca hereda el `device_id` de NVS (usa siempre `ESP32_DEMO_01`). Si NVS
  guarda un `device_id` distinto —el firmware demo se instaló sobre un nodo productivo aprovisionado—, la
  identidad queda bloqueada: el nodo captura en LittleFS pero no entrega token al cliente MQTT ni
  publica (`core::resolverIdentidad`, pruebas nativas). El backend además rechaza `simulated:true` de
  dispositivos no marcados como demo (BE-015).

## ADR-09 · Dos tareas en el núcleo 1, captura con más prioridad

- Captura (prioridad 3) y red (2) en el núcleo 1; la pila Wi-Fi queda en el 0. La captura preempta el
  handshake TLS y **nunca** espera a la red: el mutex del buffer solo se toma por operación de cola y se
  suelta durante el PUBACK + acuse (verificado por `test_el_cerrojo_no_se_sostiene_durante_el_envio`).
- Watchdog = 2 × (TLS 10 s + CONNACK 5 s) = 30 s ≤ keep-alive 60 s (`static_assert`).

## ADR-10 · El puerto y el esquema MQTT no son configurables (HU-14)

- `MQTT_PORT` fijo en 8883: definirlo por build flag es un `#error`, y un `static_assert` en el cliente
  impide otro valor. `mqtt_host` solo acepta un nombre de host (sin `mqtt://`, sin `:1883`, sin ruta).
  Una configuración errónea deja al nodo sin conectar (y guardando en LittleFS), nunca en texto plano.

## ADR-11 · Migración a Arduino-ESP32 3.x / ESP-IDF 5.5 (IOT-003)

- **Contexto.** El firmware se fijó en Arduino core 2.0.17 (ESP-IDF 4.4) por estabilidad. Espressif
  declaró el fin de vida de la rama 4.4 en julio de 2024 (AR2024-008): la pila Wi-Fi, mbedTLS, LwIP y
  el bootloader del nodo dejaron de recibir parches de seguridad.
- **Decisión.** Plataforma pioarduino `55.03.312-1` (Arduino-ESP32 3.3.12, ESP-IDF 5.5.5), fijada por
  versión exacta. La plataforma oficial de PlatformIO no publica Arduino 3.x.
- **Cambios de código.** Solo el watchdog: en IDF 5 el core ya inicializa el TWDT, así que
  `inicializarWatchdog()` lo reconfigura (`esp_task_wdt_reconfigure`) con el mismo plazo, panic y
  vigilancia del idle de la CPU0 que tenía en 4.4. `WiFiClientSecure`, LittleFS, SNTP y la librería
  MQTT compilan sin cambios.
- **Verificado.** Compilación de `esp32dev` y `demo` sin avisos y las 77 pruebas nativas.
- **Pendiente en hardware.** Ensayo de duración en el banco (watchdog, reconexión TLS contra EMQX,
  buffer LittleFS existente, consumo de RAM en el handshake). El ocupado de flash sube a ~89 % de la
  partición de aplicación (1216 KB entonces; 1280 KB desde ADR-18).

## ADR-12 · Hora SNTP no autenticada (IOT-007)

- **Contexto.** SNTP no está autenticado: un intermediario en la red del local puede responder otra hora y
  desplazar `captured_at` de lecturas marcadas `sntp_synced`, dentro de la ventana que acepta el backend.
- **Decisión en el nodo.** Dentro de un arranque, la relación época↔uptime solo cambia por la deriva del
  oscilador (milisegundos por hora). Una sincronización posterior que la mueva más de 120 s, o una época
  fuera de 2024–2100, se descarta y se conserva la base vigente (`core::evaluarSincronizacion`, pruebas
  nativas). Los rechazos se cuentan en `estado_nodo.saltos_reloj_rechazados` y se registran en el log.
- **Límite.** La primera sincronización de cada arranque no tiene referencia en el nodo; la contrasta el
  backend con `received_at` y con la base de las lecturas previas del mismo arranque.
- **TLS.** El core compila mbedTLS sin `CONFIG_MBEDTLS_HAVE_TIME_DATE`: la vigencia de los certificados no
  se comprueba con el reloj, así que una hora manipulada no altera la validación del broker.
- **Salida futura.** Fuente de tiempo autenticada (NTS o la hora del backend por el canal TLS).

## ADR-13 · Firmware de producción endurecido (IOT-001)

- **Contexto.** Con acceso físico al nodo, `esptool.py read_flash` volcaba el token MQTT, la clave Wi-Fi y
  el buffer de lecturas en claro; también permitía grabar un firmware modificado o cambiar la CA de
  LittleFS para redirigir el nodo a otro broker.
- **Decisión.** Entorno `produccion` aparte (los de desarrollo no cambian) que recompila el framework con
  `custom_sdkconfig` (`seguridad_produccion.sdkconfig`): Secure Boot v2 con firma **externa** (la clave
  no pasa por la máquina de compilación), Flash Encryption *release*, NVS Encryption, modo de descarga
  UART desactivado, tabla de particiones en `0x10000` (`partitions_produccion.csv`, LittleFS cifrada) y
  CA del broker **embebida** en la imagen firmada (`scripts/embeber_ca.py`; sin CA no compila). El core
  deja de guardar su propia copia de SSID y clave (`WiFi.persistent(false)`).
- **Nombre del fragmento.** No puede llamarse `sdkconfig.produccion`: PlatformIO escribe ahí el
  sdkconfig completo del entorno, lo sobrescribiría y la compilación híbrida entraría en bucle.
- **Irreversible.** El primer arranque quema eFuses: procedimiento en `docs/seguridad_produccion.md`,
  solo en ESP32 v3.0+ y primero en una placa de laboratorio.
- **Verificado.** `pio run -e produccion` compila sin avisos del código propio (con una CA de
  prueba). El sdkconfig generado confirma Secure Boot v2, verificación de firma en las actualizaciones
  (`SECURE_SIGNED_ON_UPDATE`), Flash Encryption *release*, descarga UART desactivada, NVS Encryption,
  anti-rollback y la tabla en `0x10000`; los binarios salen sin firmar. Sin `root_ca.pem` la compilación
  se detiene. El aviso de `gen_esp32part` sobre el nombre `littlefs` es el mismo de desarrollo: la etiqueta
  es la que monta el firmware.
- **Pendiente en hardware.** Primer arranque con cifrado en sitio, montaje de LittleFS sobre la partición
  cifrada y aprovisionamiento por consola con NVS Encryption.

## ADR-14 · Actualización OTA verificada con vuelta atrás (IOT-004)

- **Contexto.** El nodo solo se actualizaba por USB y el backend daba por instalada una versión que nadie
  había descargado. Con Secure Boot y la descarga UART desactivada (ADR-13) el USB deja de ser opción.
- **Orden.** El backend publica en `farmacias/<device_id>/ota` (solo él puede: ACL) la versión, una URL
  HTTPS, la SHA-256 y el tamaño de la imagen. El nodo la valida (`core::parsearOrdenOTA`, pruebas nativas):
  es para él, la versión es posterior a la que corre y la URL, la huella y el tamaño son válidos.
- **Descarga.** Con la CA de confianza del nodo (embebida en producción), sin seguir redirecciones, con
  `Content-Length` igual al anunciado y la SHA-256 calculada por partes mientras se escribe en la otra
  partición; si no coincide, la imagen nunca se activa. En producción `esp_ota_end` y el bootloader exigen
  además la firma de Secure Boot. Ninguna espera bloquea más que el plazo del watchdog (`static_assert`);
  la captura sigue en su tarea durante la descarga.
- **Vuelta atrás.** `verifyRollbackLater()` deja la imagen nueva *a prueba*: se confirma cuando captura una
  lectura y conecta con el broker; si no lo logra en 15 min se invalida y el bootloader vuelve a la
  anterior, que lo informa con `ota_revertida`. Una imagen a prueba no acepta otra OTA.
- **Resultado.** Solo el nodo cierra el despliegue en el backend (`ota_confirmada`, `ota_fallida`,
  `ota_rechazada`, `ota_revertida`).
- **Verificado.** Pruebas nativas de la orden, versiones, SHA-256 por partes (vectores FIPS) y decisión de
  arranque; `esp32dev`, `demo` y `produccion` compilan. La aplicación ocupa el 88-90 % de su partición
  (1280 KB, ADR-18), y la imagen firmada de producción añade un bloque de firma de 4 KB.
- **Pendiente en hardware.** Descarga real contra un servidor HTTPS, reinicio, confirmación y reversión
  forzada (imagen que no conecta) en una placa de laboratorio.

## ADR-15 · Certificado de cliente por nodo (IOT-006)

- **Contexto.** El token MQTT es un secreto portátil: copiado del nodo o de un registro, permite suplantar
  al nodo desde cualquier equipo. BE-002 hizo efectiva la revocación y IOT-001 lo cifra en la NVS, pero no
  lo liga al hardware.
- **Decisión.** TLS mutuo opcional y gradual. El nodo genera una clave EC P-256 con mbedTLS (`tls csr`,
  solo con la radio encendida para que el RNG sea aleatorio de verdad), la guarda en NVS y exporta solo la
  CSR con `CN=<device_id>`. El certificado firmado por la CA de dispositivos se instala con
  `tls certificado <base64>` y se valida antes de guardarlo: misma clave, un único CN igual al device_id y,
  si declara usos extendidos, `clientAuth` (`src/tls/IdentidadTLS.cpp`). Al arrancar se vuelve a validar;
  si no sirve, el nodo conecta sin él.
- **Broker y backend.** EMQX local pide certificado (`verify_peer`, sin exigirlo). La identidad del nodo la
  fija la autenticación integrada de EMQX (usuario = device_id, contraseña = token). El backend emite y
  expulsa sesiones por API; el usuario lo crea o retira el backend solo en EMQX 5 completo o Dedicated, y a
  mano en la consola en EMQX Serverless (cuya API no tiene endpoints de usuarios). Revocar o rotar sigue
  cortando al nodo. El backend ya no contrasta el CN
  del certificado con el device_id: la vinculación certificado–dispositivo solo sería exigible fuera de
  EMQX Serverless, con la configuración del listener y de la autorización del broker.
- **Verificado.** En el equipo de desarrollo, con la misma versión de mbedTLS del core (3.6.6): clave, CSR
  aceptada por OpenSSL, firma con `infra-local/scripts/firmar_csr_nodo.sh` (CA EC y RSA) y validación de
  certificado válido, sin usos extendidos, de servidor, de otro CN, de otra clave y con dos CN. Pruebas
  nativas del paso base64 → PEM; `esp32dev` y `demo` compilan.
- **Límites.** La documentación de EMQX Cloud lista la autenticación X.509 también para Serverless, pero no se
  ha probado en la instancia real: hasta verificarlo, en producción los nodos se autentican con usuario y token. La aplicación ocupa ya ~93 % (demo ~95 %) de su partición: la
  próxima funcionalidad grande obliga a revisar el reparto de la flash o recortar dependencias.
- **Pendiente en hardware.** Generación de la clave y handshake mutuo contra el EMQX local.

## ADR-16 · Integridad del buffer de lecturas: CRC32 + Flash Encryption, sin HMAC (IOT-009)

- **Contexto.** Cada registro del buffer lleva un CRC32, que detecta errores accidentales pero no una
  manipulación: con acceso a la flash se podía alterar una lectura pendiente y recalcular el CRC.
- **Análisis.** Un HMAC por registro necesita una clave en el propio nodo. Sin Flash Encryption esa clave
  se lee con la misma facilidad que el buffer, así que no añade protección; con Flash Encryption *release*
  (ADR-13) la descarga UART y JTAG están desactivadas y la única vía es desoldar la memoria. Ahí, cambiar
  un bloque cifrado produce 32 bytes pseudoaleatorios al descifrar, que el CRC32 rechaza (probabilidad de
  pasar 2⁻³²) y el registro va a cuarentena; reponer un bloque cifrado antiguo de la misma dirección
  devuelve una lectura ya enviada, con su `boot_id`/`seq_no` original, que el backend descarta como
  duplicada, y la lectura sustituida aparece como hueco de secuencia. Ninguna de las dos altera la
  evidencia sin dejar rastro. El canal ya está autenticado: TLS, token y certificado de cliente por nodo
  (ADR-15), ACL por tópico y acuse con prueba de contenido (IOT-008).
- **Decisión.** No se cambia el formato del registro (evita perder el buffer de los nodos en campo al
  actualizar). La protección depende de que la flash esté cifrada, así que el nodo lo informa en cada
  `arranque` (`flash_cifrada`, `arranque_seguro`) y en `estado`, y avisa en el log si no lo está.
- **Salida futura.** Si el modelo de amenazas lo exige, firmar cada registro al capturarlo con la clave
  del nodo de ADR-15 (ECDSA P-256) y verificar en el backend con su certificado.

## ADR-17 · Token de relevo para rotar sin cortar al nodo legítimo (HU-44, A-1)

- **Contexto.** EMQX guarda una sola contraseña por usuario. En Serverless el administrador la cambia a
  mano en la consola. Con un único token, cualquier orden deja al nodo legítimo sin conexión un rato: si se
  cambia primero el broker, el nodo no entra hasta que alguien le escriba el token nuevo por la consola
  serie; si se cambia primero el nodo, no entra hasta que se actualice el broker. Las lecturas no se
  pierden (quedan en LittleFS), pero el tablero pasa a SIN_DATOS.
- **Decisión.** El nodo admite un segundo token en NVS, `mqtt_relevo`, que se carga **antes** de cambiar el
  broker. Con un relevo cargado, el nodo lo prueba **primero** en cada conexión (`core::elegirTokenConexion`).
  Si el broker lo acepta, lo promueve a `mqtt_token`, **borra el token anterior en ese momento** y publica
  `credencial_rotada` con `origen: relevo`: el anterior nunca queda como respaldo. Si el broker rechaza el
  relevo por credencial (CONNACK 4 «bad user name or password» o 5 «not authorized», MQTT 3.1.1), la consola
  aún no cambió: el nodo entra con el vigente sin reiniciar y vuelve a probar el relevo en la siguiente
  reconexión. Mientras siga conectado con el vigente y haya un relevo, cada 5 min cierra la sesión con un
  DISCONNECT ordenado para probarlo (`sondearRelevo`), por si el backend no pudo expulsar la sesión al
  confirmar la consola: cambiar la contraseña en EMQX no cierra las sesiones abiertas.
- **Canal.** Consola serie protegida con la clave de servicio (IOT-002), no un mensaje MQTT: el token nunca
  viaja por la red ni queda en el broker, y una ACL mal puesta no permitiría entregar tokens a otro nodo.
- **Procedimiento de rotación (orden).**
  1. Backend: `POST /api/dispositivos/{id}/credenciales/rotar` → token nuevo (se muestra una vez; queda
     `broker_pendiente = aprovisionar`).
  2. Nodo: `desbloquear <clave_servicio>` y `nvs set mqtt_relevo <token nuevo>`.
  3. Consola de EMQX: cambiar la contraseña del usuario `<device_id>` por el token nuevo.
  4. Backend: `POST /api/dispositivos/{id}/broker/confirmar`. El backend expulsa la sesión; el nodo
     reconecta, prueba el relevo, el broker lo acepta y el nodo borra el token viejo (evento
     `credencial_rotada`, `origen: relevo`).
  Si la rotación fue por **compromiso** (`causa: compromiso`), entre los pasos 1 y 4 el backend retiene la
  telemetría sin acuse: el nodo la conserva en LittleFS y la entrega tras el paso 4 (Backend
  `docs/operacion/HU-44_rotacion_y_revocacion.md`).
- **Revocación.** No usa relevo: se retira el usuario en la consola y el backend rechaza la telemetría del
  nodo mientras tanto (revocación a nivel de aplicación).
- **Verificado.** Pruebas nativas de la decisión (`test_credenciales.cpp`); `esp32dev` compila. En hardware
  (27/10/2026) contra el EMQX local con el backend en modo manual: rotación preventiva y por compromiso
  completas, relevo rechazado con la consola pendiente y aceptado al confirmar, 28/28 lecturas retenidas
  entregadas (`docs/evidencias/HU-44_rotacion_relevo_evidencia.md`).
  **Pendiente** contra EMQX Cloud Serverless: el mismo ciclo editando la consola real.

## ADR-18 · Particiones de 4 MB: más espacio de aplicación para OTA, LittleFS medido (A-2)

- **Contexto.** Con app0/app1 de 1216 KB la imagen ocupaba el 92,6-94,8 % de su partición
  (`produccion`, `esp32dev`, `demo`). Con ese margen (≈ 65-90 KB) una OTA con algo más de código podía no
  caber, y la OTA exige que la imagen nueva entre en la otra ranura. LittleFS tenía 1600 KB, más de lo que
  usa el buffer.
- **Cálculo de B.** El buffer de 30 000 lecturas son 300 segmentos de 100 × 40 B = 4000 B; cada uno ocupa
  un bloque de 4096 B. A eso se suman la reserva libre que exige `guardar` (16 KB, `BUFFER_RESERVA_BYTES`)
  y 16 bloques para metadatos de directorio (300 entradas) y copia en escritura:
  `core::bytesMinimosParticion` = (300 + 16) × 4096 + 16 384 = **1 310 720 B (1280 KB)**. Con menos, la
  cola se satura antes de su capacidad nominal (rechaza escribir cuando quedan < 16 KB libres).
- **Decisión.** `partitions.csv`: app0/app1 **1280 KB** (`0x140000`), LittleFS **1472 KB** (`0x170000`,
  48 bloques de holgura). `partitions_produccion.csv` (tabla en `0x10000` y `nvs_keys`): app0/app1
  1280 KB, LittleFS **1408 KB** (`0x160000`, 32 bloques de holgura). `OTA_TAMANO_MAXIMO` = `0x140000` en el
  nodo y en el backend.
- **Resultado.** `esp32dev` 88,8 %, `demo` 90,1 %, `produccion` 88,1 % (1 154 399 B, medido con una CA
  pública temporal). Margen para OTA: 126-156 KB.
- **Medición en el nodo.** `estado_nodo` publica `fs_total_bytes` (B real de LittleFS montado) y
  `fs_libres_bytes`. Al arrancar, el nodo registra un error si B < 1280 KB. El Paper debe citar el
  `fs_total_bytes` del nodo del piloto y N = 30 000 (o ⌊(B − reserva − metadatos)/4096⌋ × 100 si B fuese
  menor).
- **Costo.** Cambiar la tabla exige flasheo por USB (una OTA no la cambia) y borra LittleFS. Se hace ahora,
  antes del piloto y antes de quemar los eFuses del entorno `produccion`.
- **Medido en la placa (27/10/2026).** `fs_total_bytes = 1 507 328 B` (1472 KB) en el primer `estado_nodo`;
  con la CA local, `esp32dev` 88,9 %, `demo` 90,2 %, `produccion` 88,0 %
  (`docs/evidencias/HU-44_rotacion_relevo_evidencia.md`, sección 6).
- **Pendiente en hardware.** Llenar el buffer hasta la saturación para confirmar que entran las 30 000
  lecturas.

