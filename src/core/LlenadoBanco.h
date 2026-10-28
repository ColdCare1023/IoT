#ifndef CORE_LLENADO_BANCO_H
#define CORE_LLENADO_BANCO_H

#include <atomic>
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

/// Orden que aplicó la tarea de captura (ver `BuzonLlenadoBanco::aplicar`).
enum class OrdenBanco { Ninguna, Llenar, Cancelar };

/**
 * Órdenes de la consola para el llenado (`buffer llenar <n>`, `buffer cancelar`).
 *
 * La consola corre en la tarea de red y el llenado en la de captura. Cuando las
 * dos escribían el contador, `buffer cancelar` podía perderse: el descuento lee,
 * resta y escribe, y un 0 puesto por la consola entre la lectura y la escritura
 * quedaba sobrescrito. Ahora la consola solo deja la orden aquí y la tarea de
 * captura es la única que escribe el contador. El intercambio atómico no pierde
 * órdenes; si llegan dos antes de aplicarse, vale la última.
 */
class BuzonLlenadoBanco {
public:
    /// Consola. `n` entre 1 y la capacidad del buffer.
    void pedirLlenado(uint32_t n) {
        if (n != SIN_ORDEN && n != CANCELAR) _orden.store(n);
    }
    /// Consola.
    void pedirCancelacion() { _orden.store(CANCELAR); }

    /// Consola: true cuando la tarea de captura ya aplicó la última orden y no
    /// le quedan sintéticos por escribir. `buffer descartar` lo espera para
    /// que ningún sintético llegue después del descarte.
    bool detenido() const { return _orden.load() == SIN_ORDEN && !_llenando.load(); }

    /// Tarea de captura, al empezar cada paso: aplica la orden pendiente al
    /// contador, que solo ella escribe.
    OrdenBanco aplicar(uint32_t& porLlenar) {
        const uint32_t orden = _orden.exchange(SIN_ORDEN);
        OrdenBanco aplicada = OrdenBanco::Ninguna;
        if (orden == CANCELAR) {
            porLlenar = 0;
            aplicada = OrdenBanco::Cancelar;
        } else if (orden != SIN_ORDEN) {
            porLlenar = orden;
            aplicada = OrdenBanco::Llenar;
        }
        _llenando.store(porLlenar != 0);
        return aplicada;
    }

private:
    static constexpr uint32_t SIN_ORDEN = 0;
    static constexpr uint32_t CANCELAR = UINT32_MAX;
    std::atomic<uint32_t> _orden{SIN_ORDEN};
    std::atomic<bool> _llenando{false};
};

}  // namespace core

#endif  // CORE_LLENADO_BANCO_H
