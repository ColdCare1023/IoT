#include "Ota.h"

#include <cstdlib>

#include "Ack.h"
#include "Json.h"

namespace core {

static bool parsearComponente(const std::string& texto, size_t& pos, uint16_t& valor) {
    if (pos >= texto.size() || texto[pos] < '0' || texto[pos] > '9') return false;
    uint32_t acumulado = 0;
    size_t digitos = 0;
    while (pos < texto.size() && texto[pos] >= '0' && texto[pos] <= '9') {
        acumulado = acumulado * 10 + (uint32_t)(texto[pos] - '0');
        if (++digitos > 5 || acumulado > 65535) return false;
        ++pos;
    }
    valor = (uint16_t)acumulado;
    return true;
}

bool parsearVersion(const std::string& texto, VersionFirmware& version) {
    VersionFirmware v;
    size_t pos = 0;
    if (!parsearComponente(texto, pos, v.mayor) || pos >= texto.size() || texto[pos++] != '.') return false;
    if (!parsearComponente(texto, pos, v.menor) || pos >= texto.size() || texto[pos++] != '.') return false;
    if (!parsearComponente(texto, pos, v.parche) || pos != texto.size()) return false;
    version = v;
    return true;
}

bool esPosterior(const VersionFirmware& c, const VersionFirmware& a) {
    if (c.mayor != a.mayor) return c.mayor > a.mayor;
    if (c.menor != a.menor) return c.menor > a.menor;
    return c.parche > a.parche;
}

static bool urlAceptable(const std::string& url) {
    const std::string esquema = "https://";
    if (url.size() <= esquema.size() || url.size() > 256 || url.compare(0, esquema.size(), esquema) != 0) {
        return false;
    }
    for (char c : url) {
        if (c <= ' ' || c == '"' || c == '\\' || c == 0x7f) return false;
    }
    return true;
}

bool parsearOrdenOTA(const std::string& json, const std::string& deviceId, OrdenOTA& orden,
                     std::string& motivo) {
    std::string destino;
    if (!leerCadena(json, "device_id", destino) || destino != deviceId) {
        motivo = "device_id";
        return false;
    }
    OrdenOTA o;
    VersionFirmware v;
    if (!leerCadena(json, "version", o.version) || !parsearVersion(o.version, v)) {
        motivo = "version";
        return false;
    }
    if (!leerCadena(json, "url", o.url) || !urlAceptable(o.url)) {
        motivo = "url";
        return false;
    }
    if (!leerCadena(json, "sha256", o.sha256) || !huellaValida(o.sha256)) {
        motivo = "sha256";
        return false;
    }
    if (!leerSinSigno32(json, "tamano", o.tamano) || o.tamano == 0 || o.tamano > OTA_TAMANO_MAXIMO) {
        motivo = "tamano";
        return false;
    }
    orden = o;
    return true;
}

DecisionArranque evaluarArranque(bool imagenAPrueba, bool capturoLectura, bool conectoBroker,
                                 uint32_t msDesdeArranque, uint32_t plazoMs) {
    if (!imagenAPrueba) return DecisionArranque::Confirmar;
    if (capturoLectura && conectoBroker) return DecisionArranque::Confirmar;
    return msDesdeArranque >= plazoMs ? DecisionArranque::Revertir : DecisionArranque::Esperar;
}

}  // namespace core
