#ifndef CORE_TIEMPO_H
#define CORE_TIEMPO_H

#include <cstdint>
#include <string>

namespace core {

/**
 * Tiempo del nodo: época UTC por SNTP + uptime monótono.
 *
 * El ESP32 no tiene RTC con batería. La versión anterior, sin NTP, anclaba el
 * timestamp a la hora de COMPILACIÓN: fabricaba una fecha plausible pero
 * falsa. Aquí no se inventa nada:
 *
 *   - `sntp_synced`     → la época viene de SNTP sincronizado en ESTE arranque.
 *   - `sntp_backfilled` → la lectura se capturó sin hora, pero en el mismo
 *                         arranque en que luego llegó SNTP: su hora se deriva
 *                         del uptime monótono (captured_at = uptime + desplazamiento).
 *                         Es exacta salvo la deriva del oscilador (~ppm).
 *   - `unsynced`        → no hay forma honesta de fecharla: captured_at = null.
 *                         El backend conserva boot_id/seq_no/uptime_s.
 *
 * `uptime_s` viaja SIEMPRE, así una lectura `unsynced` puede re-fecharse
 * después en el backend si llega información de otro arranque.
 */

enum class CalidadTiempo : uint8_t { Sincronizado, Retrofechado, SinSincronizar };

const char* nombreCalidadTiempo(CalidadTiempo calidad);

/// Relación época↔uptime conocida en un arranque concreto.
struct BaseTiempo {
    bool valida = false;
    uint32_t bootId = 0;
    int64_t desplazamientoS = 0;  ///< época − uptime_s

    /// Fija la base a partir de una época SNTP leída en `uptimeS`.
    void fijar(uint32_t boot, uint32_t epoch, uint32_t uptimeS) {
        valida = true;
        bootId = boot;
        desplazamientoS = (int64_t)epoch - (int64_t)uptimeS;
    }
};

/// Época mínima aceptada como "reloj real" (2024-01-01T00:00:00Z). Antes de
/// SNTP el ESP32 arranca en 1970.
static const uint32_t EPOCH_MINIMA_VALIDA = 1704067200u;
/// Época máxima plausible (2100-01-01T00:00:00Z): una respuesta SNTP más allá
/// es un error o una manipulación, no una hora.
static const uint32_t EPOCH_MAXIMA_PLAUSIBLE = 4102444800u;

/**
 * IOT-007: SNTP no está autenticado. Un atacante en la red del local puede
 * responder con otra hora y desplazar captured_at de lecturas marcadas como
 * sincronizadas. Dentro de un mismo arranque la relación época↔uptime solo
 * varía por la deriva del oscilador (milisegundos por hora), así que una
 * sincronización posterior que la mueva más de la tolerancia se RECHAZA y se
 * conserva la base vigente. La primera sincronización de cada arranque no
 * tiene referencia en el nodo: la contrasta el backend con received_at.
 */
static const uint32_t TOLERANCIA_SALTO_RELOJ_S = 120;

enum class DecisionSincronizacion : uint8_t { Aceptar, FueraDeRango, SaltoRechazado };

DecisionSincronizacion evaluarSincronizacion(const BaseTiempo& vigente, uint32_t bootId, uint32_t epoch,
                                             uint32_t uptimeS,
                                             uint32_t toleranciaS = TOLERANCIA_SALTO_RELOJ_S);

/// Resuelve la hora de una lectura. `epochCaptura` es 0 si no había SNTP al
/// capturar. Devuelve la calidad y, si aplica, la época en `epochSalida`.
CalidadTiempo resolverTiempo(uint32_t epochCaptura, uint32_t bootIdLectura,
                             uint32_t uptimeLecturaS, const BaseTiempo& base,
                             uint32_t& epochSalida);

/// "2026-09-25T15:30:00Z". Sin `gmtime()` (estado global no reentrante) ni
/// `mktime()` (interpreta hora local).
std::string formatearISO8601(uint32_t epoch);

}  // namespace core

#endif  // CORE_TIEMPO_H
