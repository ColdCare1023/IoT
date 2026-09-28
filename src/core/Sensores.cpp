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

}  // namespace core
