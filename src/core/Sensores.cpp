#include "Sensores.h"

namespace core {

const char* codigoFallo(FalloSensor fallo) {
    switch (fallo) {
        case FalloSensor::SinRespuesta: return "no_response";
        case FalloSensor::ErrorCrc:     return "crc_error";
        case FalloSensor::Timeout:      return "timeout";
        case FalloSensor::Implausible:  return "implausible";
        case FalloSensor::ValorPowerOn: return "power_on_reset";
        case FalloSensor::NoInstalado:  return "not_installed";
        case FalloSensor::Ninguno:
        default:                        return nullptr;
    }
}

uint8_t crc8Maxim(const uint8_t* datos, size_t longitud) {
    uint8_t crc = 0;
    for (size_t i = 0; i < longitud; ++i) {
        uint8_t byte = datos[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint8_t mezcla = (uint8_t)((crc ^ byte) & 0x01);
            crc >>= 1;
            if (mezcla) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

uint8_t crc8Sensirion(const uint8_t* datos, size_t longitud) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < longitud; ++i) {
        crc ^= datos[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

LecturaSensor decodificarScratchpadDS18B20(const uint8_t scratchpad[9]) {
    bool todosFF = true;
    bool todosCero = true;
    for (int i = 0; i < 9; ++i) {
        if (scratchpad[i] != 0xFF) todosFF = false;
        if (scratchpad[i] != 0x00) todosCero = false;
    }
    if (todosFF || todosCero) return LecturaSensor::falla(FalloSensor::SinRespuesta);

    if (crc8Maxim(scratchpad, 8) != scratchpad[8]) {
        return LecturaSensor::falla(FalloSensor::ErrorCrc);
    }

    int raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);

    // Bits R1:R0 del registro de configuración (byte 4).
    switch ((scratchpad[4] >> 5) & 0x03) {
        case 0: raw &= ~7; break;  //  9 bits
        case 1: raw &= ~3; break;  // 10 bits
        case 2: raw &= ~1; break;  // 11 bits
        default: break;            // 12 bits
    }

    if (raw == DS18B20_RAW_POWER_ON) return LecturaSensor::falla(FalloSensor::ValorPowerOn);

    const float tempC = (float)raw / 16.0f;
    if (tempC < DS18B20_MIN_C || tempC > DS18B20_MAX_C) {
        return LecturaSensor::falla(FalloSensor::Implausible);
    }
    return LecturaSensor::ok(tempC);
}

LecturaSHT31 decodificarSHT31(const uint8_t trama[6]) {
    LecturaSHT31 r;

    if (crc8Sensirion(&trama[0], 2) != trama[2]) {
        r.temperatura = LecturaSensor::falla(FalloSensor::ErrorCrc);
    } else {
        const uint16_t st = (uint16_t)(((uint16_t)trama[0] << 8) | trama[1]);
        const float t = -45.0f + 175.0f * (float)st / 65535.0f;
        r.temperatura = (t < SHT31_T_MIN_C || t > SHT31_T_MAX_C)
                            ? LecturaSensor::falla(FalloSensor::Implausible)
                            : LecturaSensor::ok(t);
    }

    return r;
}

LecturaSHT31 falloSHT31(FalloSensor fallo) {
    LecturaSHT31 r;
    r.temperatura = LecturaSensor::falla(fallo);
    return r;
}

}  // namespace core
