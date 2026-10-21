# Fabricación segura del nodo (IOT-001)

> **Operaciones IRREVERSIBLES.** El primer arranque de una imagen del entorno `produccion` quema eFuses del
> ESP32: clave de Flash Encryption, clave pública de Secure Boot, desactivación de JTAG y del modo de descarga
> UART. Un error inutiliza la placa. Probar siempre primero en una **placa de laboratorio** y no aplicar en
> campo hasta que la actualización OTA firmada (IOT-004) esté operativa: después de este procedimiento la placa
> **ya no puede reprogramarse por USB**.

## Qué protege

| Riesgo (auditoría IOT-001) | Control |
|---|---|
| Leer por USB el token MQTT, la clave Wi-Fi o la huella de la clave de servicio | NVS Encryption (claves XTS en `nvs_keys`, cifrada por Flash Encryption) |
| Leer la aplicación o el buffer de lecturas | Flash Encryption *release* (app, `nvs_keys`, `otadata` y `littlefs` cifradas) |
| Instalar un firmware modificado (p. ej. que oculte excursiones) | Secure Boot v2: el bootloader solo arranca imágenes firmadas con nuestra clave |
| Sustituir la CA para redirigir el nodo a otro broker | CA embebida en la imagen firmada (`scripts/embeber_ca.py`); LittleFS ya no se consulta |
| Reprogramar o volcar la flash por el puerto serie | Modo de descarga UART desactivado (`UART_DOWNLOAD_DIS`) y JTAG desactivado |
| Volver a una versión vulnerable | Anti-rollback por `secure_version` en eFuse (IOT-004) |

La configuración está en `seguridad_produccion.sdkconfig` y `partitions_produccion.csv` (entorno `produccion` de
`platformio.ini`). Los entornos `esp32dev` y `demo` no cambian: son para desarrollo y banco.

## Requisitos

1. **ESP32 revisión v3.0 (ECO3) o superior** (Secure Boot v2). Comprobar antes de nada:
   ```bash
   esptool.py --chip esp32 chip_id        # debe indicar "revision v3.0" o superior
   ```
   Un DevKitC con ESP32-WROOM-32D (revisión 1) **no** sirve; usar ESP32-WROOM-32E.
2. Clave de firma de Secure Boot v2, generada **fuera de línea** y custodiada fuera del repositorio y de CI
   (idealmente en un HSM o un equipo sin red), con copia de respaldo cifrada. Sin ella no habrá más
   actualizaciones para los nodos firmados.
   ```bash
   espsecure.py generate_signing_key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
   ```
3. Certificado de la CA del broker en `data/certs/root_ca.pem` (ver `data/certs/README.md`). El entorno
   `produccion` no compila sin él.

## Procedimiento por nodo

1. **Compilar** (binario reproducible, IOT-010) en una copia del proyecto cuya ruta **no tenga espacios**
   (la compilación híbrida de ESP-IDF falla con ellos) y con un núcleo de PlatformIO **propio**:
   ```bash
   export PLATFORMIO_CORE_DIR="$HOME/.platformio-produccion"
   SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) pio run -e produccion
   ```
   La compilación híbrida reemplaza las librerías del framework Arduino del núcleo que usa: compartido con
   `esp32dev`/`demo`, estos dejan de enlazar (`undefined reference to __wrap_log_printf`) hasta borrar
   `packages/framework-arduinoespressif32*` para que PlatformIO los reinstale.
   Los binarios salen **sin firmar** (`CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` desactivado): la clave no
   pasa por la máquina de compilación.
2. **Firmar** bootloader y aplicación en el equipo que custodia la clave:
   ```bash
   espsecure.py sign_data --version 2 --keyfile secure_boot_signing_key.pem \
       --output bootloader-signed.bin .pio/build/produccion/bootloader.bin
   espsecure.py sign_data --version 2 --keyfile secure_boot_signing_key.pem \
       --output firmware-signed.bin .pio/build/produccion/firmware.bin
   ```
3. **Grabar** en la placa virgen (offsets de `partitions_produccion.csv`; la tabla va en `0x10000`):
   ```bash
   esptool.py --chip esp32 --port PUERTO write_flash \
       0x1000  bootloader-signed.bin \
       0x10000 .pio/build/produccion/partitions.bin \
       0x18000 ota_data_initial.bin \
       0x20000 firmware-signed.bin
   ```
4. **Primer arranque** (no cortar la alimentación): el bootloader genera la clave de cifrado, cifra la flash
   en sitio y quema los eFuses de Secure Boot y Flash Encryption; la aplicación desactiva el modo de descarga
   UART. Tarda alrededor de un minuto.
5. **Aprovisionar** por la consola serie (las claves de NVS Encryption se generan solas en el primer uso):
   `nvs set wifi_ssid …`, `wifi_pass`, `mqtt_host`, `mqtt_token`, `device_id` y **`clave_servicio`**
   (IOT-002), y `reiniciar`. Si el broker admite TLS mutuo (IOT-006): `desbloquear …`, `tls csr` (la clave
   se genera en el nodo y queda en la NVS cifrada), firmar la CSR con la CA de dispositivos (uso
   `clientAuth`, CN = device_id), `tls certificado <base64>` y `reiniciar`.
6. **Verificar**: el evento `arranque` llega con `consola_protegida: true`; `esptool.py read_flash` ya no
   responde (modo de descarga desactivado); un `firmware.bin` sin firmar no arranca.
7. Registrar en el backend la huella SHA-256 del `firmware-signed.bin` instalado (`firmware_releases`).

## Actualizaciones

Tras el procedimiento solo es posible actualizar por **OTA** (IOT-004, ver `docs/contrato_payload_v1.md`
§3.1):

1. Subir `FW_MAYOR/FW_MENOR/FW_PARCHE` (el nodo rechaza versiones no posteriores), compilar y **firmar**
   `firmware.bin` como en el paso 2.
2. Publicar `firmware-signed.bin` en un servidor HTTPS cuya CA raíz esté en el `root_ca.pem` embebido
   (sin redirecciones: el nodo no las sigue).
3. Registrar la release en el backend con `hash_sha256` (de `firmware-signed.bin`), `url_descarga` y
   `tamano_bytes`, programar el despliegue y ejecutarlo: el backend publica la orden en
   `farmacias/<device_id>/ota`.
4. El nodo descarga, comprueba tamaño y SHA-256, y el bootloader exige la firma de Secure Boot. La imagen
   arranca **a prueba**: se confirma (`ota_confirmada`) cuando captura una lectura y conecta con el broker;
   si no lo logra en 15 min, vuelve a la anterior (`ota_revertida`).

Para retirar del servicio una versión vulnerable, subir `CONFIG_BOOTLOADER_APP_SECURE_VERSION` en
`seguridad_produccion.sdkconfig`: el eFuse anti-rollback impide volver a imágenes con un valor menor.

## Estado de verificación en este repositorio

- `esp32dev` y `demo`: compilan y pasan las pruebas nativas en CI.
- `produccion`: compila (ver ADR-13 en `docs/decisiones_tecnicas.md`). **No se ha ejecutado en
  hardware**: el primer ensayo debe hacerse en una placa de laboratorio, sabiendo que queda
  bloqueada para siempre en ese modo.
