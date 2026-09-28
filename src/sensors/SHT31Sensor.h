#ifndef SHT31_SENSOR_H
#define SHT31_SENSOR_H

#include <Arduino.h>

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
 */
class SHT31Sensor {
public:
    SHT31Sensor(uint8_t direccion, int pinSda, int pinScl);
    bool begin();
    core::LecturaSHT31 leer();

private:
    bool _comando(uint16_t comando);
    uint8_t _direccion;
    int _sda;
    int _scl;
};

#endif  // SHT31_SENSOR_H
