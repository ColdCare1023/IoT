#include "BitacoraPuertaLittleFS.h"

#include <LittleFS.h>

#include "../config.h"

static const char* const RUTA_BITACORA = "/tt/puerta.bin";

bool BitacoraPuertaLittleFS::Archivo::leer(std::vector<uint8_t>& datos) {
    datos.clear();
    if (!LittleFS.exists(RUTA_BITACORA)) return false;
    File f = LittleFS.open(RUTA_BITACORA, "r");
    if (!f) return false;
    const size_t n = f.size();
    datos.resize(n);
    const bool ok = n == 0 || f.read(datos.data(), n) == n;
    f.close();
    return ok;
}

bool BitacoraPuertaLittleFS::Archivo::escribir(const std::vector<uint8_t>& datos) {
    // "w" + close: LittleFS confirma el archivo entero al cerrarlo (copia en
    // escritura); un corte a mitad deja la versión anterior.
    File f = LittleFS.open(RUTA_BITACORA, "w");
    if (!f) return false;
    const bool ok = f.write(datos.data(), datos.size()) == datos.size();
    f.close();
    return ok;
}

BitacoraPuertaLittleFS::BitacoraPuertaLittleFS()
    : _mutex(xSemaphoreCreateMutex()), _bitacora(_archivo, CAPACIDAD_EVENTOS_PUERTA) {}

bool BitacoraPuertaLittleFS::begin() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    // LittleFS.totalBytes() es 0 si la partición no se montó.
    _disponible = LittleFS.totalBytes() > 0;
    bool legible = true;
    if (_disponible) legible = _bitacora.montar();
    const uint32_t pendientes = _bitacora.pendientes();
    xSemaphoreGive(_mutex);
    if (!_disponible) {
        LOG_E("Puerta", "LittleFS no disponible: los cambios de puerta iran a la cola en RAM.");
    } else if (!legible) {
        LOG_E("Puerta", "%s ilegible: se empieza una bitacora vacia.", RUTA_BITACORA);
    } else if (pendientes > 0) {
        LOG_I("Puerta", "%lu cambio(s) de puerta sin publicar recuperados.", (unsigned long)pendientes);
    }
    return _disponible;
}

core::ResultadoAnotacion BitacoraPuertaLittleFS::anotar(bool abierta, uint32_t bootId, uint32_t uptimeS,
                                                         uint32_t epoch) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const core::ResultadoAnotacion r = _bitacora.anotar(abierta, bootId, uptimeS, epoch);
    xSemaphoreGive(_mutex);
    return r;
}

bool BitacoraPuertaLittleFS::primero(core::EventoPuerta& evento) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool hay = _bitacora.primero(evento);
    xSemaphoreGive(_mutex);
    return hay;
}

bool BitacoraPuertaLittleFS::confirmar(uint32_t id) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool ok = _bitacora.confirmar(id);
    xSemaphoreGive(_mutex);
    return ok;
}

#define CON_MUTEX(expr)                       \
    xSemaphoreTake(_mutex, portMAX_DELAY);    \
    const uint32_t _valor = (expr);           \
    xSemaphoreGive(_mutex);                   \
    return _valor

uint32_t BitacoraPuertaLittleFS::pendientes() { CON_MUTEX(_bitacora.pendientes()); }
uint32_t BitacoraPuertaLittleFS::descartados() { CON_MUTEX(_bitacora.descartados()); }
uint32_t BitacoraPuertaLittleFS::corrupciones() { CON_MUTEX(_bitacora.corrupciones()); }

#undef CON_MUTEX
