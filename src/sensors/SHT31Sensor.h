#ifndef SHT31_SENSOR_H
#define SHT31_SENSOR_H

#include <Arduino.h>

#include "../core/BusI2C.h"
#include "../core/Sensores.h"

/**
 * SHT31 — temperatura y humedad del AMBIENTE de la botica (HU-02, HU-03).
 *
 * Va dentro del gabinete exterior, junto a las ranuras de ventilación. Bus
 * I²C: SDA = GPIO 21, SCL = GPIO 22, dirección 0x44 (ADDR a GND).
 *
 * Medición "single shot", alta repetibilidad, sin clock stretching (0x2400),
 * con CRC-8 Sensirion por palabra. Si el sensor no responde tras
 * SHT31_REINTENTOS, ambas magnitudes salen como SENSOR_FAULT — sin tocar la
 * lectura del DS18B20, que es independiente (HU-02 esc. 2).
 *
 * Recuperación (HU-02): tras SHT31_FALLAS_ANTES_DE_RECUPERAR mediciones
 * seguidas sin comunicación, `leer()` libera el bus (hasta 9 pulsos de SCL y
 * STOP) y vuelve a llamar a `begin()`. La política está en core/BusI2C.h.
 */
class SHT31Sensor {
public:
    SHT31Sensor(uint8_t direccion, int pinSda, int pinScl);
    bool begin();
    core::LecturaSHT31 leer();
    /// Veces que se liberó el bus y se reinició el sensor desde el arranque.
    uint32_t recuperaciones() const { return _vigilante.recuperaciones(); }

private:
    bool _comando(uint16_t comando);
    core::LecturaSHT31 _medir();
    void _recuperarBus();
    uint8_t _direccion;
    int _sda;
    int _scl;
    core::VigilanteFallasI2C _vigilante;
};

#endif  // SHT31_SENSOR_H
