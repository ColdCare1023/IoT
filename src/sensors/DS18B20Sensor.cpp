#include "DS18B20Sensor.h"

#include "../config.h"
#include "../system/Watchdog.h"

// Comandos 1-Wire / DS18B20 (hoja de datos Maxim).
static const uint8_t CMD_SKIP_ROM = 0xCC;
static const uint8_t CMD_CONVERT_T = 0x44;
static const uint8_t CMD_READ_SCRATCHPAD = 0xBE;
static const uint8_t CMD_WRITE_SCRATCHPAD = 0x4E;
static const uint8_t CONFIG_12_BITS = 0x7F;

DS18B20Sensor::DS18B20Sensor(uint8_t pin) : _bus(pin) {}

bool DS18B20Sensor::_configurarResolucion() {
    if (!_bus.reset()) return false;
    _bus.write(CMD_SKIP_ROM);
    _bus.write(CMD_WRITE_SCRATCHPAD);
    _bus.write(0x4B);  // TH (alarma, no se usa)
    _bus.write(0x46);  // TL (alarma, no se usa)
    _bus.write(CONFIG_12_BITS);
    return true;
}

void DS18B20Sensor::begin() {
    _presenteAntes = _configurarResolucion();
    if (_presenteAntes) {
        LOG_I("DS18B20", "Sonda detectada en GPIO%d (12 bits).", PIN_DS18B20);
    } else {
        LOG_E("DS18B20", "Sin pulso de presencia en GPIO%d: revisar cableado y pull-up 4,7 kOhm.",
              PIN_DS18B20);
    }
}

core::LecturaSensor DS18B20Sensor::leer() {
    // 1. Presencia. Sin ella no tiene sentido esperar una conversión.
    if (!_bus.reset()) {
        _presenteAntes = false;
        return core::LecturaSensor::falla(core::FalloSensor::SinRespuesta);
    }
    // Sonda reconectada en caliente: vuelve con la configuración de su
    // EEPROM; se fuerza de nuevo 12 bits antes de convertir.
    if (!_presenteAntes) {
        _presenteAntes = _configurarResolucion();
        LOG_I("DS18B20", "Sonda recuperada tras una falla previa.");
        if (!_bus.reset()) return core::LecturaSensor::falla(core::FalloSensor::SinRespuesta);
    }

    // 2. Conversión con timeout. Con alimentación de 3 hilos el sensor
    //    mantiene el bus en 0 mientras convierte y lo suelta al terminar.
    _bus.write(CMD_SKIP_ROM);
    _bus.write(CMD_CONVERT_T, 0);
    const unsigned long inicio = millis();
    while (_bus.read_bit() == 0) {
        if (millis() - inicio > DS18B20_TIMEOUT_CONVERSION_MS) {
            LOG_E("DS18B20", "Timeout de conversion (> %d ms).", DS18B20_TIMEOUT_CONVERSION_MS);
            return core::LecturaSensor::falla(core::FalloSensor::Timeout);
        }
        alimentarWatchdog();
        delay(10);
    }

    // 3. Scratchpad completo (9 bytes, el último es el CRC).
    if (!_bus.reset()) {
        _presenteAntes = false;
        return core::LecturaSensor::falla(core::FalloSensor::SinRespuesta);
    }
    _bus.write(CMD_SKIP_ROM);
    _bus.write(CMD_READ_SCRATCHPAD);
    uint8_t scratchpad[9];
    for (int i = 0; i < 9; ++i) scratchpad[i] = _bus.read();

    // 4. CRC, power-on reset y rango físico: todo en core (probado en host).
    const core::LecturaSensor lectura = core::decodificarScratchpadDS18B20(scratchpad);
    if (!lectura.valida()) {
        LOG_E("DS18B20", "SENSOR_FAULT: %s", core::codigoFallo(lectura.fallo));
        if (lectura.fallo == core::FalloSensor::SinRespuesta) _presenteAntes = false;
    }
    return lectura;
}
