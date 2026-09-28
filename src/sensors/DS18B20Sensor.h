#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>

#include "../core/Sensores.h"

/**
 * DS18B20 — sonda interior, junto a los medicamentos (HU-01).
 *
 * Bus 1-Wire en GPIO 4 con pull-up EXTERNO de 4,7 kΩ a 3,3 V y alimentación
 * normal de tres hilos (no parásita). Un único sensor por bus: se usa
 * SKIP ROM.
 *
 * Se habla con el sensor directamente por OneWire, sin DallasTemperature,
 * para tener control explícito de las tres cosas que exige el requisito:
 *   - timeout de conversión (no bloquea el bucle más de ~1 s),
 *   - verificación CRC-8 del scratchpad en CADA lectura,
 *   - motivo de falla distinguible (sin respuesta / CRC / timeout /
 *     implausible / power-on reset) en vez de un −127 °C ambiguo.
 *
 * La decodificación y las reglas viven en `core::decodificarScratchpadDS18B20`,
 * probada en el host.
 */
class DS18B20Sensor {
public:
    explicit DS18B20Sensor(uint8_t pin);

    /// Fija la resolución a 12 bits (en RAM del sensor). No falla el arranque
    /// si la sonda no está: se reintenta en cada lectura.
    void begin();

    /// Convierte y lee. Bloquea como máximo DS18B20_TIMEOUT_CONVERSION_MS,
    /// alimentando el watchdog. Nunca devuelve un código de error como valor.
    core::LecturaSensor leer();

private:
    bool _configurarResolucion();
    OneWire _bus;
    bool _presenteAntes = false;
};

#endif  // DS18B20_SENSOR_H
