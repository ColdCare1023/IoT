#ifndef CORE_ESTADO_NODO_H
#define CORE_ESTADO_NODO_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Reconexion.h"
#include "Registro.h"
#include "Tiempo.h"

namespace core {

/**
 * `detail` del evento `estado_nodo` (latido cada 5 min; HU-48 esc. 1, C-5).
 *
 * Se arma aquí, sin Arduino, para poder probar en el host qué campos lleva y
 * que el peor caso cabe en los límites del backend (32 claves y 1024 B en
 * `detail`, `MAX_CLAVES_DETALLE` / `MAX_BYTES_DETALLE` de contrato_v1.py) y en
 * el buffer MQTT del nodo.
 *
 * Campos añadidos en la v1.2.1 (solo se agregan; los anteriores no cambian):
 *   - `pendientes_desde` / `pendientes_hasta` (HU-48 esc. 1): `captured_at`
 *     del pendiente más antiguo y del más nuevo, con la misma regla que la
 *     telemetría; null si no hay pendientes, si ese registro no tiene hora
 *     honesta (arranque sin SNTP) o si no se pudo leer. El backend ya los
 *     acepta (`registrar_estado_nodo.interpretar_estado_nodo`): con los dos
 *     presentes el rango deja de ser estimado.
 *   - `reconexion_*` (HU-08 esc. 3): el episodio de reconexión en curso o el
 *     último cerrado, que sobrevive a un reinicio por watchdog (NVS).
 *   - `puerta_pendientes` / `puerta_descartados` (HU-04): eventos de puerta
 *     guardados en LittleFS sin publicar y los que no cupieron.
 *   - `sht31_recuperaciones` (HU-02): veces que se liberó el bus I²C y se
 *     reinició el SHT31 desde el arranque.
 */
struct DatosEstadoNodo {
    uint32_t pendientes = 0;
    uint32_t ocupacionPct = 0;
    uint32_t capacidad = 0;
    uint32_t fsTotalBytes = 0;
    uint32_t fsLibresBytes = 0;
    uint32_t perdidasTotal = 0;
    uint32_t cuarentena = 0;
    uint32_t cuarentenaDesbordada = 0;
    uint32_t ultimoSeqNo = 0;
    int32_t rssiDbm = 0;
    uint32_t heapLibre = 0;
    uint32_t heapMinimo = 0;
    uint32_t intervaloS = 0;
    uint32_t pubacksSinAck = 0;
    uint32_t acksHuellaAjena = 0;
    uint32_t eventosDescartados = 0;
    bool relojSntp = false;
    uint32_t saltosRelojRechazados = 0;

    // HU-48: extremos de la cola de pendientes (si se pudieron leer).
    bool hayPrimerPendiente = false;
    RegistroLectura primerPendiente;
    bool hayUltimoPendiente = false;
    RegistroLectura ultimoPendiente;

    // HU-08: episodio de reconexión (en curso o el último cerrado).
    EpisodioReconexion reconexion;

    // HU-04: bitácora de puerta.
    uint32_t puertaPendientes = 0;
    uint32_t puertaDescartados = 0;

    // HU-02: recuperaciones del bus del SHT31.
    uint32_t sht31Recuperaciones = 0;
};

/// `captured_at` de un registro según la regla de la telemetría. false si no
/// tiene hora honesta (queda null).
bool instanteDeRegistro(const RegistroLectura& r, const BaseTiempo& base, uint32_t& epoch);

std::string detalleEstadoNodo(const DatosEstadoNodo& d, const BaseTiempo& base);

/// Límites del backend para `detail` (contrato_v1.py).
static const size_t MAX_CLAVES_DETALLE_BACKEND = 32;
static const size_t MAX_BYTES_DETALLE_BACKEND = 1024;

/**
 * HU-13 esc. 2: antes de un DISCONNECT voluntario el nodo publica el motivo
 * (`desconexion_ordenada`), así queda dicho por qué se fue sin LWT.
 * `motivo`: `rotacion_credencial`, `prueba_relevo`, `ota_revertida`.
 */
std::string detalleDesconexionOrdenada(const char* motivo);

}  // namespace core

#endif  // CORE_ESTADO_NODO_H
