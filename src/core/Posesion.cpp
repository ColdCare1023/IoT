#include "Posesion.h"

#include <cstring>

#include "Json.h"
#include "Sensores.h"
#include "Sha256.h"
#include "Tiempo.h"

namespace core {

namespace {

const char* const PREFIJO_CLAVE = "coldcare-pop-v1:";
const size_t BLOQUE = 64;

std::string aHex(const uint8_t* datos, size_t largo) {
    static const char hex[] = "0123456789abcdef";
    std::string salida(largo * 2, '0');
    for (size_t i = 0; i < largo; ++i) {
        salida[2 * i] = hex[datos[i] >> 4];
        salida[2 * i + 1] = hex[datos[i] & 0x0f];
    }
    return salida;
}

bool esNonceValido(const std::string& nonce) {
    if (nonce.size() < 16 || nonce.size() > 64 || nonce.size() % 2 != 0) return false;
    for (char c : nonce) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace

std::string hmacSha256Hex(const uint8_t* clave, size_t largoClave, const std::string& mensaje) {
    uint8_t k[BLOQUE] = {0};
    if (largoClave > BLOQUE) {
        Sha256 h;
        h.actualizar(clave, largoClave);
        h.finalizar(k);
    } else {
        std::memcpy(k, clave, largoClave);
    }
    uint8_t ipad[BLOQUE];
    uint8_t opad[BLOQUE];
    for (size_t i = 0; i < BLOQUE; ++i) {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }
    uint8_t interno[32];
    Sha256 h1;
    h1.actualizar(ipad, BLOQUE);
    h1.actualizar(reinterpret_cast<const uint8_t*>(mensaje.data()), mensaje.size());
    h1.finalizar(interno);

    uint8_t externo[32];
    Sha256 h2;
    h2.actualizar(opad, BLOQUE);
    h2.actualizar(interno, sizeof(interno));
    h2.finalizar(externo);
    return aHex(externo, sizeof(externo));
}

std::string respuestaPosesion(const std::string& token, const std::string& deviceId,
                              const std::string& nonce) {
    const std::string material = std::string(PREFIJO_CLAVE) + token;
    uint8_t clave[32];
    Sha256 h;
    h.actualizar(reinterpret_cast<const uint8_t*>(material.data()), material.size());
    h.finalizar(clave);
    return hmacSha256Hex(clave, sizeof(clave), nonce + ":" + deviceId);
}

bool parsearDesafioPosesion(const std::string& json, const std::string& deviceId, std::string& nonce) {
    std::string tipo;
    std::string destino;
    std::string valor;
    if (!leerCadena(json, "tipo", tipo) || tipo != "desafio_posesion") return false;
    if (!leerCadena(json, "device_id", destino) || destino != deviceId) return false;
    if (!leerCadena(json, "nonce", valor) || !esNonceValido(valor)) return false;
    nonce = valor;
    return true;
}

std::string detalleLecturaVigente(const RegistroLectura& r) {
    ObjetoJSON d;
    d.sinSigno("boot_id", r.bootId);
    d.sinSigno("seq_no", r.seqNo);
    if (r.falloDs == FalloSensor::Ninguno && r.tempDsCenti != NULO_I16) {
        d.real("temperatura_interna", r.tempDsCenti / 100.0, 2);
    } else {
        d.nulo("temperatura_interna");
    }
    if (r.tiene(BANDERA_TIEMPO_SNTP) && r.capturadoEpoch != 0) {
        d.cadena("captured_at", formatearISO8601(r.capturadoEpoch));
        d.cadena("time_quality", "synced");
    } else {
        d.nulo("captured_at");
        d.cadena("time_quality", "unsynced");
    }
    d.sinSigno("uptime_s", r.uptimeS);
    return d.cerrar();
}

}  // namespace core
