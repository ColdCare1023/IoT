#include "BusI2C.h"

namespace core {

static bool falloDeBus(FalloSensor f) {
    return f == FalloSensor::SinRespuesta || f == FalloSensor::Timeout || f == FalloSensor::ErrorCrc;
}

bool falloDeComunicacion(const LecturaSHT31& l) {
    return !l.temperatura.valida() && !l.humedad.valida() && falloDeBus(l.temperatura.fallo) &&
           falloDeBus(l.humedad.fallo);
}

bool VigilanteFallasI2C::registrar(const LecturaSHT31& lectura) {
    if (!falloDeComunicacion(lectura)) {
        _consecutivas = 0;
        return false;
    }
    if (++_consecutivas < _umbral) return false;
    _consecutivas = 0;
    _recuperaciones++;
    return true;
}

ResultadoLiberacionI2C liberarBusI2C(PinesBusI2C& p) {
    ResultadoLiberacionI2C r;
    p.sda(true);
    p.scl(true);
    p.esperarMedioCiclo();
    while (!p.leerSda() && r.pulsos < PULSOS_LIBERACION_I2C) {
        p.scl(false);
        p.esperarMedioCiclo();
        p.scl(true);
        p.esperarMedioCiclo();
        r.pulsos++;
    }
    // STOP: SDA baja con SCL bajo, sube SCL y luego SDA.
    p.scl(false);
    p.esperarMedioCiclo();
    p.sda(false);
    p.esperarMedioCiclo();
    p.scl(true);
    p.esperarMedioCiclo();
    p.sda(true);
    p.esperarMedioCiclo();
    r.sdaLibre = p.leerSda();
    return r;
}

}  // namespace core
