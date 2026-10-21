#include "Consola.h"

#include "Sha256.h"

namespace core {

bool comandoModifica(const std::string& verbo) {
    return !(verbo == "estado" || verbo == "rssi" || verbo == "ayuda" || verbo == "help" ||
             verbo == "desbloquear" || verbo == "bloquear");
}

bool claveServicioAceptable(const std::string& clave) {
    if (clave.size() < CLAVE_SERVICIO_MIN) return false;
    for (char c : clave) {
        if (c == ' ' || c == '\t') return false;
    }
    return true;
}

std::string huellaClaveServicio(const std::string& clave) {
    // Prefijo de dominio: la misma clave no produce la misma huella en otro uso.
    return sha256Hex(std::string("thermotrace-consola-v1:") + clave);
}

bool claveServicioCorrecta(const std::string& candidata, const std::string& huellaGuardada) {
    if (huellaGuardada.size() != 64) return false;
    return igualesTiempoConstante(huellaClaveServicio(candidata), huellaGuardada);
}

bool FrenoIntentos::puedeIntentar(uint32_t ahoraMs) const {
    if (_fallos < MAX_INTENTOS) return true;
    return (uint32_t)(ahoraMs - _ultimoFalloMs) >= ESPERA_MS;  // tolera el desborde de millis()
}

void FrenoIntentos::registrarFallo(uint32_t ahoraMs) {
    ++_fallos;
    _ultimoFalloMs = ahoraMs;
}

void FrenoIntentos::registrarAcierto() { _fallos = 0; }

bool EstadoConsola::bloqueada(bool aprovisionado, bool hayClaveServicio, uint32_t ahoraMs) const {
    if (!aprovisionado || !hayClaveServicio) return false;
    if (!_desbloqueada) return true;
    return (uint32_t)(ahoraMs - _desdeMs) >= VENTANA_MS;
}

void EstadoConsola::desbloquear(uint32_t ahoraMs) {
    _desbloqueada = true;
    _desdeMs = ahoraMs;
}

void EstadoConsola::bloquear() { _desbloqueada = false; }

}  // namespace core
