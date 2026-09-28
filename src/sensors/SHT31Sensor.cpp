#include "SHT31Sensor.h"

#include <Wire.h>

#include "../config.h"
#include "../system/Watchdog.h"

static const uint16_t CMD_MEDIR_ALTA_REPETIBILIDAD = 0x2400;  // single shot, sin clock stretching
static const uint16_t CMD_RESET_SUAVE = 0x30A2;
static const uint16_t CMD_CALEFACTOR_OFF = 0x3066;
static const uint32_t ESPERA_MEDICION_MS = 16;  // 15,5 ms máx. a alta repetibilidad

SHT31Sensor::SHT31Sensor(uint8_t direccion, int pinSda, int pinScl)
    : _direccion(direccion), _sda(pinSda), _scl(pinScl) {}

bool SHT31Sensor::_comando(uint16_t comando) {
    Wire.beginTransmission(_direccion);
    Wire.write((uint8_t)(comando >> 8));
    Wire.write((uint8_t)(comando & 0xFF));
    return Wire.endTransmission() == 0;  // 0 = ACK de dirección y datos
}

bool SHT31Sensor::begin() {
    Wire.begin(_sda, _scl);
    Wire.setTimeOut(SHT31_TIMEOUT_I2C_MS);
    const bool ok = _comando(CMD_RESET_SUAVE);
    delay(2);
    if (ok) {
        _comando(CMD_CALEFACTOR_OFF);  // el calefactor interno falsearía la temperatura ambiente
        LOG_I("SHT31", "Detectado en 0x%02X (SDA=%d, SCL=%d).", _direccion, _sda, _scl);
    } else {
        LOG_E("SHT31", "Sin ACK en 0x%02X: revisar SDA=GPIO%d / SCL=GPIO%d y ADDR.", _direccion,
              _sda, _scl);
    }
    return ok;
}

core::LecturaSHT31 SHT31Sensor::leer() {
    core::LecturaSHT31 ultimo = core::falloSHT31(core::FalloSensor::SinRespuesta);

    for (int intento = 0; intento < SHT31_REINTENTOS; ++intento) {
        if (intento > 0) {
            alimentarWatchdog();
            delay(20);
        }
        if (!_comando(CMD_MEDIR_ALTA_REPETIBILIDAD)) {
            ultimo = core::falloSHT31(core::FalloSensor::SinRespuesta);
            continue;
        }
        delay(ESPERA_MEDICION_MS);

        const int recibidos = Wire.requestFrom((int)_direccion, 6);
        if (recibidos != 6) {
            while (Wire.available()) Wire.read();
            ultimo = core::falloSHT31(core::FalloSensor::Timeout);
            continue;
        }
        uint8_t trama[6];
        for (int i = 0; i < 6; ++i) trama[i] = (uint8_t)Wire.read();

        ultimo = core::decodificarSHT31(trama);
        // Un CRC malo suele ser ruido transitorio del cable: se reintenta.
        // Implausible no: repetir la medición no lo arregla.
        if (ultimo.temperatura.fallo == core::FalloSensor::ErrorCrc) continue;
        return ultimo;
    }

    LOG_E("SHT31", "SENSOR_FAULT tras %d intentos: %s", SHT31_REINTENTOS,
          core::codigoFallo(ultimo.temperatura.fallo));
    return ultimo;
}
