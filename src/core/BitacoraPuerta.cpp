#include "BitacoraPuerta.h"

#include "Json.h"
#include "Registro.h"

namespace core {

static void ponerU16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t leerU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void ponerU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static uint32_t leerU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Formato (little-endian):
//   cabecera 16 B: 'P''T''A''1', n (u16), reservado (u16), siguiente id (u32), descartados (u32)
//   n entradas de 20 B: id, boot_id, uptime_s, época (u32), abierta (u8), 3 B reservados
//   CRC32 (u32) de todo lo anterior
std::vector<uint8_t> codificarBitacora(const std::deque<EventoPuerta>& eventos, uint32_t siguienteId,
                                       uint32_t descartados) {
    const size_t n = eventos.size();
    std::vector<uint8_t> s(TAM_CABECERA_BITACORA + n * TAM_ENTRADA_BITACORA + 4, 0);
    s[0] = 'P';
    s[1] = 'T';
    s[2] = 'A';
    s[3] = '1';
    ponerU16(&s[4], (uint16_t)n);
    ponerU32(&s[8], siguienteId);
    ponerU32(&s[12], descartados);
    size_t o = TAM_CABECERA_BITACORA;
    for (const EventoPuerta& e : eventos) {
        ponerU32(&s[o], e.id);
        ponerU32(&s[o + 4], e.bootId);
        ponerU32(&s[o + 8], e.uptimeS);
        ponerU32(&s[o + 12], e.epoch);
        s[o + 16] = e.abierta ? 1 : 0;
        o += TAM_ENTRADA_BITACORA;
    }
    ponerU32(&s[o], crc32(s.data(), o));
    return s;
}

bool decodificarBitacora(const std::vector<uint8_t>& s, uint16_t capacidad, std::deque<EventoPuerta>& eventos,
                         uint32_t& siguienteId, uint32_t& descartados) {
    if (s.size() < TAM_CABECERA_BITACORA + 4) return false;
    if (s[0] != 'P' || s[1] != 'T' || s[2] != 'A' || s[3] != '1') return false;
    const size_t n = leerU16(&s[4]);
    if (n > capacidad || s.size() != TAM_CABECERA_BITACORA + n * TAM_ENTRADA_BITACORA + 4) return false;
    const size_t fin = s.size() - 4;
    if (crc32(s.data(), fin) != leerU32(&s[fin])) return false;

    std::deque<EventoPuerta> leidos;
    size_t o = TAM_CABECERA_BITACORA;
    for (size_t i = 0; i < n; ++i, o += TAM_ENTRADA_BITACORA) {
        EventoPuerta e;
        e.id = leerU32(&s[o]);
        e.bootId = leerU32(&s[o + 4]);
        e.uptimeS = leerU32(&s[o + 8]);
        e.epoch = leerU32(&s[o + 12]);
        if (s[o + 16] > 1) return false;
        e.abierta = s[o + 16] == 1;
        leidos.push_back(e);
    }
    eventos = leidos;
    siguienteId = leerU32(&s[8]);
    descartados = leerU32(&s[12]);
    return true;
}

BitacoraPuerta::BitacoraPuerta(AlmacenBitacora& almacen, uint16_t capacidad)
    : _almacen(almacen), _capacidad(capacidad == 0 ? 1 : capacidad) {}

bool BitacoraPuerta::montar() {
    std::vector<uint8_t> datos;
    if (!_almacen.leer(datos) || datos.empty()) return true;  // primera vez: vacía
    std::deque<EventoPuerta> eventos;
    uint32_t siguiente = 1, descartados = 0;
    if (!decodificarBitacora(datos, _capacidad, eventos, siguiente, descartados)) {
        _corrupciones++;
        _eventos.clear();
        return false;
    }
    _eventos = eventos;
    _descartados = descartados;
    // El id nunca retrocede, aunque la cabecera diga otra cosa.
    uint32_t mayor = 0;
    for (const EventoPuerta& e : _eventos) mayor = e.id > mayor ? e.id : mayor;
    _siguienteId = siguiente > mayor ? siguiente : mayor + 1;
    if (_siguienteId == 0) _siguienteId = 1;
    return true;
}

ResultadoAnotacion BitacoraPuerta::anotar(bool abierta, uint32_t bootId, uint32_t uptimeS, uint32_t epoch) {
    if (_eventos.size() >= _capacidad) {
        _descartados++;
        _persistir();
        return ResultadoAnotacion::Descartado;
    }
    EventoPuerta e;
    e.id = _siguienteId++;
    if (_siguienteId == 0) _siguienteId = 1;
    e.bootId = bootId;
    e.uptimeS = uptimeS;
    e.epoch = epoch;
    e.abierta = abierta;
    _eventos.push_back(e);
    return _persistir() ? ResultadoAnotacion::Guardado : ResultadoAnotacion::SinPersistir;
}

bool BitacoraPuerta::primero(EventoPuerta& evento) const {
    if (_eventos.empty()) return false;
    evento = _eventos.front();
    return true;
}

bool BitacoraPuerta::confirmar(uint32_t id) {
    if (_eventos.empty() || _eventos.front().id != id) return false;
    _eventos.pop_front();
    _persistir();
    return true;
}

bool BitacoraPuerta::_persistir() {
    return _almacen.escribir(codificarBitacora(_eventos, _siguienteId, _descartados));
}

std::string detalleEventoPuerta(const EventoPuerta& e) {
    ObjetoJSON d;
    d.booleano("door_open", e.abierta);
    return d.cerrar();
}

}  // namespace core
