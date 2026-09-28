#ifndef CORE_PAYLOAD_H
#define CORE_PAYLOAD_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Registro.h"
#include "Tiempo.h"

namespace core {

/**
 * Contrato de telemetría v1.0 (HU-05). Ver `docs/contrato_payload_v1.md`.
 *
 * Reglas que esta serialización garantiza:
 *   - Los campos de cada sensor habilitado se emiten SIEMPRE. Si la lectura
 *     no es válida el valor sale como `null` explícito junto a
 *     `*_status = "SENSOR_FAULT"` y `*_fault = <motivo>`; nunca se omite y
 *     nunca vale 0.0 ni −127.
 *   - `sensor_fault` es true si CUALQUIER sensor térmico/ambiental falló.
 *     Una falla del SHT31 no invalida la lectura del DS18B20 (HU-02 esc. 2).
 *   - Sin MC-38 instalado: `door_open = null`, `mc38_status = "not_installed"`
 *     y no se genera ninguna señal de puerta (HU-04 esc. 4).
 *   - `received_at` NO lo pone el nodo: lo agrega el backend al recibir.
 *   - Si el resultado excede `maxBytes` no se publica nada (HU-05 esc. 3).
 */

static const char* const SCHEMA_VERSION_TELEMETRIA = "1.0";
static const size_t PAYLOAD_MAX_BYTES = 768;
static const size_t DEVICE_ID_MAX = 50;

/// "v1.2.0" a partir de los tres componentes guardados en el registro.
std::string textoVersionFirmware(uint8_t mayor, uint8_t menor, uint8_t parche);

/// device_id: 1..50 caracteres [A-Za-z0-9_-]. Es usuario MQTT y parte del
/// tópico, así que no puede traer '/', '+', '#' ni espacios.
bool deviceIdValido(const std::string& deviceId);

/// Validación estructural de un registro antes de publicarlo. Si falla, el
/// registro no se publica: va a cuarentena con `motivo`.
bool validarRegistro(const RegistroLectura& r, std::string& motivo);

/// Serializa la telemetría. Devuelve "" si el registro no es válido o si el
/// JSON excede `maxBytes`: en ambos casos no debe publicarse.
std::string serializarTelemetria(const RegistroLectura& r, const std::string& deviceId,
                                 const BaseTiempo& base,
                                 size_t maxBytes = PAYLOAD_MAX_BYTES);

/**
 * Evento de diagnóstico en `.../eventos`. `detalleJSON` es un objeto ya
 * serializado (p. ej. de `ObjetoJSON`) o "" para `null`.
 * `epochEvento` es 0 si no había SNTP cuando ocurrió.
 */
std::string serializarEvento(const std::string& deviceId, uint32_t bootId,
                             const char* tipoEvento, uint32_t epochEvento,
                             uint32_t uptimeEventoS, const BaseTiempo& base,
                             const std::string& detalleJSON);

/// Last Will: lo publica el broker, así que no puede llevar hora ni uptime.
std::string serializarLWT(const std::string& deviceId);

}  // namespace core

#endif  // CORE_PAYLOAD_H
