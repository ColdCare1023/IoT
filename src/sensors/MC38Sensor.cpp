#include "MC38Sensor.h"

#include "../config.h"

MC38Sensor::MC38Sensor(uint8_t pin) : _pin(pin), _antirrebote(MC38_DEBOUNCE_MS) {}

void MC38Sensor::begin() {
    pinMode(_pin, INPUT_PULLUP);
    const bool abiertaAhora = digitalRead(_pin) == HIGH;
    _antirrebote.inicializar(abiertaAhora, (uint32_t)millis());
    LOG_I("MC38", "GPIO%d, estado inicial: %s.", _pin, abiertaAhora ? "ABIERTA" : "cerrada");
}

bool MC38Sensor::poll() {
    const bool cruda = digitalRead(_pin) == HIGH;
    if (_antirrebote.muestrear(cruda, (uint32_t)millis())) {
        LOG_I("MC38", "Puerta %s.", _antirrebote.estaAbierta() ? "ABIERTA" : "cerrada");
        return true;
    }
    return false;
}

bool MC38Sensor::abierta() const { return _antirrebote.estaAbierta(); }
bool MC38Sensor::huboApertura() const { return _antirrebote.huboApertura(); }

uint32_t MC38Sensor::segundosAbiertaEnVentana() const {
    return _antirrebote.duracionSegundos((uint32_t)millis());
}

uint32_t MC38Sensor::rachaSegundos() const {
    return _antirrebote.rachaSegundos((uint32_t)millis());
}

void MC38Sensor::cerrarVentana() { _antirrebote.cerrarVentana((uint32_t)millis()); }
