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
