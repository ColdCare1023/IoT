#ifndef MC38_SENSOR_H
#define MC38_SENSOR_H

#include <Arduino.h>

#include "../core/Antirrebote.h"

/**
 * MC-38 — reed switch de puerta, OPCIONAL (HU-04).
 *
 * Contacto seco entre GPIO 15 y GND, con el pull-up interno del ESP32:
 *   LOW  = imán presente  = puerta CERRADA
 *   HIGH = imán ausente   = puerta ABIERTA
 *
 * `poll()` se llama cada 50 ms; el antirrebote de 50 ms descarta el rebote
 * mecánico del contacto. Entre dos payloads (30 s) se acumula si hubo apertura
 * y cuánto duró, porque una apertura de 10–20 s cabe entera entre dos
 * muestras y se perdería mirando solo el estado instantáneo.
 *
 * Toda la lógica de estados vive en `core::Antirrebote` (probada en host).
 * Si el nodo se compila con MC38_INSTALADO=0 esta clase no se usa: un pin sin
 * sensor flota en HIGH, que se leería como "abierta" permanente.
 */
class MC38Sensor {
public:
    explicit MC38Sensor(uint8_t pin);
    void begin();
    void poll();

    bool abierta() const;
    bool huboApertura() const;
    uint32_t segundosAbiertaEnVentana() const;
    uint32_t rachaSegundos() const;
    void cerrarVentana();

private:
    uint8_t _pin;
    core::Antirrebote _antirrebote;
};

#endif  // MC38_SENSOR_H
