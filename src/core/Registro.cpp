#include "Registro.h"

#include <cmath>

namespace core {

uint32_t crc32(const uint8_t* datos, size_t longitud) {
    // Bit a bit, sin tabla: los registros son de 40 bytes y ahorrar 1 KB de
    // flash importa más que unos microsegundos por lectura.
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < longitud; ++i) {
        crc ^= datos[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return ~crc;
}

static void ponerU16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void ponerU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t leerU16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t leerU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void codificarRegistro(const RegistroLectura& r, uint8_t s[TAM_REGISTRO]) {
    s[0] = 'T';
    s[1] = 'R';
    s[2] = VERSION_FORMATO_REGISTRO;
    s[3] = r.banderas;
    ponerU32(&s[4], r.bootId);
    ponerU32(&s[8], r.seqNo);
    ponerU32(&s[12], r.capturadoEpoch);
    ponerU32(&s[16], r.uptimeS);
    ponerU16(&s[20], (uint16_t)r.tempDsCenti);
    ponerU16(&s[22], (uint16_t)r.tempShtCenti);
    ponerU16(&s[24], r.humShtCenti);
    s[26] = (uint8_t)r.falloDs;
    s[27] = (uint8_t)r.falloShtT;
    s[28] = (uint8_t)r.falloShtH;
    s[29] = r.fwMayor;
    s[30] = r.fwMenor;
    s[31] = r.fwParche;
    ponerU16(&s[32], r.puertaAbiertaS);
    ponerU16(&s[34], r.puertaRachaS);
    ponerU32(&s[36], crc32(s, 36));
}

static bool falloValido(uint8_t v) {
    return v <= (uint8_t)FalloSensor::NoInstalado;
}

bool decodificarRegistro(const uint8_t e[TAM_REGISTRO], RegistroLectura& r) {
    if (e[0] != 'T' || e[1] != 'R' || e[2] != VERSION_FORMATO_REGISTRO) return false;
    if (crc32(e, 36) != leerU32(&e[36])) return false;
    if (!falloValido(e[26]) || !falloValido(e[27]) || !falloValido(e[28])) return false;

    r.banderas = e[3];
    r.bootId = leerU32(&e[4]);
    r.seqNo = leerU32(&e[8]);
    r.capturadoEpoch = leerU32(&e[12]);
    r.uptimeS = leerU32(&e[16]);
    r.tempDsCenti = (int16_t)leerU16(&e[20]);
    r.tempShtCenti = (int16_t)leerU16(&e[22]);
    r.humShtCenti = leerU16(&e[24]);
    r.falloDs = (FalloSensor)e[26];
    r.falloShtT = (FalloSensor)e[27];
    r.falloShtH = (FalloSensor)e[28];
    r.fwMayor = e[29];
    r.fwMenor = e[30];
    r.fwParche = e[31];
    r.puertaAbiertaS = leerU16(&e[32]);
    r.puertaRachaS = leerU16(&e[34]);
    return true;
}

int16_t aCentesimas(float valor) {
    if (!std::isfinite(valor)) return NULO_I16;
    const double c = std::round((double)valor * 100.0);
    if (c <= (double)NULO_I16 || c > 32767.0) return NULO_I16;
    return (int16_t)c;
}

uint16_t aCentesimasSinSigno(float valor) {
    if (!std::isfinite(valor) || valor < 0.0f) return NULO_U16;
    const double c = std::round((double)valor * 100.0);
    if (c >= (double)NULO_U16) return NULO_U16;
    return (uint16_t)c;
}

float deCentesimas(int16_t centesimas) {
    if (centesimas == NULO_I16) return NAN;
    return (float)centesimas / 100.0f;
}

float deCentesimasSinSigno(uint16_t centesimas) {
    if (centesimas == NULO_U16) return NAN;
    return (float)centesimas / 100.0f;
}

void fijarDS18B20(RegistroLectura& r, const LecturaSensor& lectura) {
    if (lectura.valida()) {
        r.tempDsCenti = aCentesimas(lectura.valor);
        r.falloDs = (r.tempDsCenti == NULO_I16) ? FalloSensor::Implausible : FalloSensor::Ninguno;
    } else {
        r.tempDsCenti = NULO_I16;
        r.falloDs = lectura.fallo;
    }
}

void fijarSHT31(RegistroLectura& r, const LecturaSHT31& lectura) {
    if (lectura.temperatura.valida()) {
        r.tempShtCenti = aCentesimas(lectura.temperatura.valor);
        r.falloShtT = (r.tempShtCenti == NULO_I16) ? FalloSensor::Implausible : FalloSensor::Ninguno;
    } else {
        r.tempShtCenti = NULO_I16;
        r.falloShtT = lectura.temperatura.fallo;
    }
    if (lectura.humedad.valida()) {
        r.humShtCenti = aCentesimasSinSigno(lectura.humedad.valor);
        r.falloShtH = (r.humShtCenti == NULO_U16) ? FalloSensor::Implausible : FalloSensor::Ninguno;
    } else {
        r.humShtCenti = NULO_U16;
        r.falloShtH = lectura.humedad.fallo;
    }
}

}  // namespace core
