# Checklist antes de instalar el nodo en el local (piloto)

Cada punto deja evidencia (captura o log) en `docs/evidencias/`.

## Firmware y perfil (IOT-001, B-22)

- [ ] Se instala el entorno **`produccion`** (`pio run -e produccion`), no `esp32dev`: Secure Boot v2,
      Flash Encryption, NVS cifrada y descarga UART desactivada. Seguir `seguridad_produccion.md` (eFuses
      irreversibles: primero en una placa de laboratorio).
- [ ] Ocupación de la partición de aplicación medida con el binario de producción
      (`pio run -e produccion` imprime «Flash: … %») y anotada. Por encima del 95 % no se habilita OTA
      hasta revisar el reparto (ver «Partición» abajo).
- [ ] `nvs set clave_servicio <clave>`: la consola queda bloqueada en el nodo aprovisionado. En el arranque
      el evento `arranque` debe traer `consola_protegida: true`.
- [ ] Las capturas de evidencia se toman con la consola **bloqueada**: el monitor serie nunca muestra
      valores de `nvs set` (solo «valor no mostrado»), pero una consola abierta permite leer el estado.

## Intervalo de captura (B-20)

- [ ] Las pruebas de banco usaron `intervalo_s = 10`. Antes del piloto: `nvs borrar intervalo_s` (vuelve
      al valor de fábrica, 30 s) y comprobar en `estado` → `intervalo_s: 30`.
- [ ] El backend recibe el intervalo en `estado_nodo` (`intervalo_s`) y recalcula el umbral SIN_DATOS
      (3 × 30 s = 90 s); comprobarlo en `/api/dispositivos/estado-operativo` (`umbral_sin_datos_segundos`).
- [ ] Las cifras de los documentos (buffer de 10,4 días, 90 s de SIN_DATOS) asumen 30 s.

## Pines (B-21, tarea T-04 de SP-01)

| Señal | GPIO | Comprobación |
|---|---|---|
| DS18B20 (1-Wire, pull-up externo 4,7 kΩ) | 4 | lectura estable |
| SHT31 SDA / SCL | 21 / 22 | `estado` → SHT31 OK |
| MC-38 (pull-up interno) | 27 | abrir y cerrar la puerta: evento `puerta` |
| GPIO 34 | — | sin uso: el firmware ya no lee ADC en 34 (`config.h`) |
| GPIO 15 y 32 | — | libres; no conectar nada (15 es pin de strapping) |

- [ ] Foto del cableado con los pines visibles y captura de `estado`.

## Rotación de credencial (HU-44)

- [ ] Ensayo completo del procedimiento de ADR-17 (token de relevo) contra la instancia EMQX real.

## Partición (A-2)

`partitions.csv`: app0/app1 1216 KB y LittleFS 1600 KB; el buffer de 30 000 lecturas (300 archivos × 100)
necesita la LittleFS actual, así que agrandar las particiones de aplicación reduciría la capacidad del
buffer. Orden de preferencia si la imagen supera el 95 %: (1) recortar el binario (`CORE_DEBUG_LEVEL=0` ya
está; quitar `MODO_DEMO` del binario de producción; revisar librerías), (2) solo si no alcanza, mover
espacio de LittleFS a las aplicaciones y **medir de nuevo B** (capacidad real de LittleFS) para recalcular
N = ⌊B/40⌋ en el Paper.
