#include "Ack.h"

#include "Json.h"

namespace core {

static EstadoAck estadoDesdeTexto(const std::string& t) {
    if (t == "committed" || t == "commit_confirmado") return EstadoAck::Confirmado;
    if (t == "duplicate" || t == "duplicado") return EstadoAck::Duplicado;
    if (t == "rejected" || t == "rechazado") return EstadoAck::Rechazado;
    return EstadoAck::Desconocido;
}

bool parsearAck(const std::string& json, AckLogico& ack) {
    AckLogico r;
    if (!leerCadena(json, "device_id", r.deviceId) || r.deviceId.empty()) return false;
    if (!leerSinSigno32(json, "boot_id", r.bootId)) return false;
    if (!leerSinSigno32(json, "seq_no", r.seqNo)) return false;

    std::string estado;
    if (!leerCadena(json, "status", estado) && !leerCadena(json, "estado", estado)) return false;
    r.estado = estadoDesdeTexto(estado);
    if (r.estado == EstadoAck::Desconocido) return false;

    ack = r;
    return true;
}

void MemoriaAcks::anotar(uint32_t bootId, uint32_t seqNo, EstadoAck estado) {
    for (auto& e : _entradas) {
        if (e.usada && e.bootId == bootId && e.seqNo == seqNo) {
            e.estado = estado;
            return;
        }
    }
    Entrada& e = _entradas[_siguiente];
    e.bootId = bootId;
    e.seqNo = seqNo;
    e.estado = estado;
    e.usada = true;
    _siguiente = (_siguiente + 1) % CAPACIDAD;
}

bool MemoriaAcks::buscar(uint32_t bootId, uint32_t seqNo, EstadoAck& estado) const {
    for (const auto& e : _entradas) {
        if (e.usada && e.bootId == bootId && e.seqNo == seqNo) {
            estado = e.estado;
            return true;
        }
    }
    return false;
}

void MemoriaAcks::olvidar(uint32_t bootId, uint32_t seqNo) {
    for (auto& e : _entradas) {
        if (e.usada && e.bootId == bootId && e.seqNo == seqNo) e.usada = false;
    }
}

void MemoriaAcks::limpiar() {
    for (auto& e : _entradas) e.usada = false;
    _siguiente = 0;
}

}  // namespace core
