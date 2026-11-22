#ifndef CORE_BITACORA_PUERTA_H
#define CORE_BITACORA_PUERTA_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "Tiempo.h"

namespace core {

/**
 * Bitácora persistente y acotada de cambios de la puerta (HU-04 esc. 1, HU-35).
 *
 * Cada cambio estable del MC-38 (tras el antirrebote) se anota con su arranque,
 * su uptime y, si había SNTP, su época. Antes iba a la cola de eventos en RAM
 * (24 entradas compartidas con el resto del diagnóstico): un reinicio o una
 * desconexión larga con la puerta en uso los perdía. Ahora:
 *
 *   - Se guarda en LittleFS ANTES de intentar publicarlo. El archivo se
 *     reescribe entero (LittleFS confirma el archivo al cerrarlo, copia en
 *     escritura: un corte deja la versión anterior o la nueva, nunca una
 *     mezcla) y lleva magia y CRC32.
 *   - Se publica al haber sesión, el más antiguo primero, como evento `puerta`
 *     con `occurred_at` = hora del cambio (`boot_id` y `uptime_s` del arranque
 *     en que ocurrió; si ese arranque no tuvo SNTP, `occurred_at` es null y
 *     `time_quality` "unsynced"). Solo un PUBACK lo retira.
 *   - Capacidad fija. Lleno, se conservan los existentes y el cambio nuevo se
 *     DESCARTA y se cuenta (misma política que el buffer de lecturas, HU-06
 *     esc. 3): ninguna pérdida es silenciosa. El estado de la puerta de cada
 *     ventana sigue viajando en la telemetría (`door_open`), que tiene su
 *     propio buffer.
 */

struct EventoPuerta {
    uint32_t id = 0;       ///< creciente; identifica el evento al confirmarlo
    uint32_t bootId = 0;
    uint32_t uptimeS = 0;
    uint32_t epoch = 0;    ///< 0 si no había SNTP al ocurrir
    bool abierta = false;
};

/// Imagen completa de la bitácora (un archivo de LittleFS en el ESP32).
class AlmacenBitacora {
public:
    virtual ~AlmacenBitacora() = default;
    /// false si no existe o no se pudo leer.
    virtual bool leer(std::vector<uint8_t>& datos) = 0;
    /// Reemplaza el contenido entero (atómico en LittleFS al cerrar).
    virtual bool escribir(const std::vector<uint8_t>& datos) = 0;
};

enum class ResultadoAnotacion { Guardado, Descartado, SinPersistir };

static const size_t TAM_CABECERA_BITACORA = 16;
static const size_t TAM_ENTRADA_BITACORA = 20;

std::vector<uint8_t> codificarBitacora(const std::deque<EventoPuerta>& eventos, uint32_t siguienteId,
                                       uint32_t descartados);
bool decodificarBitacora(const std::vector<uint8_t>& datos, uint16_t capacidad,
                         std::deque<EventoPuerta>& eventos, uint32_t& siguienteId, uint32_t& descartados);

class BitacoraPuerta {
public:
    BitacoraPuerta(AlmacenBitacora& almacen, uint16_t capacidad);

    /// Recupera la bitácora. false si había un archivo ilegible (se empieza
    /// vacía y se cuenta en `corrupciones()`).
    bool montar();

    /// Anota un cambio. `SinPersistir`: el evento quedó en RAM y se publicará,
    /// pero un reinicio antes de la próxima escritura correcta lo perdería.
    ResultadoAnotacion anotar(bool abierta, uint32_t bootId, uint32_t uptimeS, uint32_t epoch);

    /// El más antiguo sin publicar.
    bool primero(EventoPuerta& evento) const;
    /// Retira el evento `id` si sigue siendo el más antiguo (PUBACK recibido).
    bool confirmar(uint32_t id);

    uint32_t pendientes() const { return (uint32_t)_eventos.size(); }
    uint32_t descartados() const { return _descartados; }
    uint32_t corrupciones() const { return _corrupciones; }
    uint16_t capacidad() const { return _capacidad; }

private:
    bool _persistir();

    AlmacenBitacora& _almacen;
    uint16_t _capacidad;
    std::deque<EventoPuerta> _eventos;
    uint32_t _siguienteId = 1;
    uint32_t _descartados = 0;
    uint32_t _corrupciones = 0;
};

/// `detail` del evento `puerta`.
std::string detalleEventoPuerta(const EventoPuerta& e);

}  // namespace core

#endif  // CORE_BITACORA_PUERTA_H
