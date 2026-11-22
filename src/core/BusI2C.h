#ifndef CORE_BUS_I2C_H
#define CORE_BUS_I2C_H

#include <cstdint>

#include "Sensores.h"

namespace core {

/**
 * Recuperación del SHT31 y de su bus I²C (HU-02).
 *
 * Si un corte de alimentación o una interferencia interrumpe una lectura a
 * medias, el SHT31 puede quedar sosteniendo SDA en bajo esperando más pulsos
 * de reloj. El bus queda "colgado": cada transacción sale sin ACK y el sensor
 * aparece en falla para siempre aunque esté sano, hasta reiniciar el nodo.
 *
 * Tras `umbral` mediciones seguidas con falla de COMUNICACIÓN (sin ACK,
 * timeout o CRC en las dos magnitudes) el driver libera el bus (hasta 9 pulsos
 * de SCL y una condición STOP, procedimiento "bus clear" de la especificación
 * I²C, UM10204 §3.1.16) y vuelve a inicializar el sensor (`begin()`: reset
 * suave y calefactor apagado). Una lectura implausible o con una sola palabra
 * corrupta no cuenta: el bus responde. La medición sigue saliendo como
 * SENSOR_FAULT mientras tanto (HU-02 esc. 2): esto solo devuelve el sensor al
 * servicio sin reiniciar el nodo.
 */

/// true si las dos magnitudes fallaron por comunicación (bus o sensor mudo).
bool falloDeComunicacion(const LecturaSHT31& lectura);

class VigilanteFallasI2C {
public:
    explicit VigilanteFallasI2C(uint8_t umbral) : _umbral(umbral == 0 ? 1 : umbral) {}

    /// Registra una medición. true si toca liberar el bus y reiniciar el
    /// sensor ahora; el contador vuelve a cero (la próxima recuperación exige
    /// otras `umbral` fallas seguidas).
    bool registrar(const LecturaSHT31& lectura);

    uint32_t consecutivas() const { return _consecutivas; }
    uint32_t recuperaciones() const { return _recuperaciones; }

private:
    uint8_t _umbral;
    uint32_t _consecutivas = 0;
    uint32_t _recuperaciones = 0;
};

/// Pines del bus para generar la secuencia sin Arduino. "Alto" = soltar la
/// línea (salida de drenador abierto: la sube el pull-up).
class PinesBusI2C {
public:
    virtual ~PinesBusI2C() = default;
    virtual void scl(bool alto) = 0;
    virtual void sda(bool alto) = 0;
    virtual bool leerSda() = 0;
    virtual void esperarMedioCiclo() = 0;  ///< ~5 µs (100 kHz)
};

static const uint8_t PULSOS_LIBERACION_I2C = 9;

struct ResultadoLiberacionI2C {
    uint8_t pulsos = 0;     ///< pulsos de SCL generados
    bool sdaLibre = false;  ///< SDA quedó en alto tras la secuencia
};

/// Mientras SDA siga en bajo, hasta 9 pulsos de SCL (el esclavo suelta SDA a
/// más tardar en el bit de ACK); después, STOP (SDA sube con SCL en alto).
ResultadoLiberacionI2C liberarBusI2C(PinesBusI2C& pines);

}  // namespace core

#endif  // CORE_BUS_I2C_H
