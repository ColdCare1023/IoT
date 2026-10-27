#ifndef CORE_LLENADO_BANCO_H
#define CORE_LLENADO_BANCO_H

#include <cstdint>

#include "Registro.h"

namespace core {

/**
 * ADR-18: ensayo de saturación del buffer en el banco.
 *
 * Llenar las 30 000 posiciones a intervalo real tarda días (a 10 s, unas
 * 83 h). El comando de banco `buffer llenar <n>` (solo entorno `esp32dev`,
 * ver COMANDOS_BANCO en config.h) escribe registros SINTÉTICOS por tandas
 * desde la tarea de captura. Cada uno lleva "simulated": true, así que el
 * backend solo los acepta de un dispositivo dado de alta como demo (BE-015):
 * nunca entran en la curva de un equipo real.
 */

/// Registros a escribir en esta tanda: lo pedido, sin pasar de `maxTanda` ni
/// de los huecos libres. Con el buffer lleno devuelve 0 y el llenado termina;
/// a partir de ahí es la captura real la que encuentra el buffer saturado.
inline uint32_t tandaLlenadoBanco(uint32_t porLlenar, uint32_t pendientes, uint32_t capacidad,
                                  uint32_t maxTanda) {
    if (porLlenar == 0 || pendientes >= capacidad) return 0;
    const uint32_t libres = capacidad - pendientes;
    const uint32_t n = porLlenar < maxTanda ? porLlenar : maxTanda;
    return n < libres ? n : libres;
}

/// Temperatura fija y dentro de 2-8 °C: el ensayo mide el buffer, no debe
/// abrir alertas en el dispositivo de banco.
constexpr int16_t TEMP_SINTETICA_CENTI = 500;

/// Registro sintético a partir de la última captura real (sensores de puerta
/// y energía tal cual). `epoch` 0 = sin SNTP: no se inventa la hora.
inline RegistroLectura registroSintetico(const RegistroLectura& plantilla, uint32_t seqNo, uint32_t uptimeS,
                                         uint32_t epoch) {
    RegistroLectura r = plantilla;
    r.seqNo = seqNo;
    r.uptimeS = uptimeS;
    r.capturadoEpoch = epoch;
    r.poner(BANDERA_TIEMPO_SNTP, epoch != 0);
    r.tempDsCenti = TEMP_SINTETICA_CENTI;
    r.falloDs = FalloSensor::Ninguno;
    r.poner(BANDERA_SIMULADO, true);
    return r;
}

}  // namespace core

#endif  // CORE_LLENADO_BANCO_H
