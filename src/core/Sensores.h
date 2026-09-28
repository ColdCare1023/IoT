#ifndef CORE_SENSORES_H
#define CORE_SENSORES_H

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace core {

/**
 * Validación y decodificación de los sensores, sin Arduino.
 *
 * Regla única del nodo: una lectura en la que no se puede confiar NUNCA sale
 * como número. Sale como `null` + `SENSOR_FAULT` + el motivo. Un 0,0 °C por
 * una lectura fallida sería indistinguible de una excursión real bajo 2 °C, y
 * un -127 °C (código de error de la librería Dallas) dispararía una alerta
 * crítica falsa. El backend nunca debe poder calcular una excursión a partir
 * de un código de error.
 *
 * Los rangos son los FÍSICOS de cada hoja de datos, no los operativos: filtrar
 * aquí a 2–8 °C escondería justamente las excursiones que hay que detectar.
 */

/// Motivo de falla de una lectura. Se serializa como `*_fault` en el payload.
enum class FalloSensor : uint8_t {
    Ninguno      = 0,
    SinRespuesta = 1,  ///< sin pulso de presencia 1-Wire / sin ACK I²C
    ErrorCrc     = 2,  ///< el checksum de la trama no coincide
    Timeout      = 3,  ///< la conversión no terminó en el plazo configurado
    Implausible  = 4,  ///< fuera del rango físico del sensor
    ValorPowerOn = 5,  ///< DS18B20 devolvió 85,0 °C de power-on reset (sin conversión real)
    NoInstalado  = 6,  ///< sensor opcional deshabilitado en esta configuración
};

/// Código textual estable para el payload. `nullptr` para `Ninguno`.
const char* codigoFallo(FalloSensor fallo);

/// Lectura de una magnitud: valor + motivo de falla.
struct LecturaSensor {
    float valor = NAN;
    FalloSensor fallo = FalloSensor::SinRespuesta;

    bool valida() const { return fallo == FalloSensor::Ninguno && std::isfinite(valor); }

    static LecturaSensor ok(float v) {
        LecturaSensor l;
        l.valor = v;
        l.fallo = FalloSensor::Ninguno;
        return l;
    }
    static LecturaSensor falla(FalloSensor f) {
        LecturaSensor l;
        l.valor = NAN;
        l.fallo = (f == FalloSensor::Ninguno) ? FalloSensor::SinRespuesta : f;
        return l;
    }
};

// Rangos físicos (hojas de datos).
static const float DS18B20_MIN_C = -55.0f;
static const float DS18B20_MAX_C = 125.0f;
static const float SHT31_T_MIN_C = -40.0f;
static const float SHT31_T_MAX_C = 125.0f;

/// Valor crudo de power-on reset del DS18B20 (85,0 °C = 0x0550 en 1/16 °C).
static const int16_t DS18B20_RAW_POWER_ON = 0x0550;

/// CRC-8 Dallas/Maxim (polinomio x^8+x^5+x^4+1, reflejado, init 0x00).
/// Es el del scratchpad y la ROM del DS18B20.
uint8_t crc8Maxim(const uint8_t* datos, size_t longitud);

/// CRC-8 Sensirion (polinomio 0x31, init 0xFF, sin reflejar). Protege cada
/// palabra de 16 bits que entrega el SHT31. CRC(0xBEEF) = 0x92.
uint8_t crc8Sensirion(const uint8_t* datos, size_t longitud);

/**
 * Decodifica el scratchpad de 9 bytes del DS18B20.
 *
 * Orden de verificación:
 *   1. 9 bytes 0xFF → bus en alto sin esclavo (sin respuesta).
 *   2. 9 bytes 0x00 → bus en corto a GND. OJO: el CRC de nueve ceros es 0 y
 *      "pasaría" la verificación; por eso se descarta antes.
 *   3. CRC-8 Maxim de los bytes 0..7 contra el byte 8.
 *   4. 0x0550 (85,0 °C) → valor de power-on reset: el sensor se reinició y
 *      no convirtió. Es físicamente posible, pero incompatible con el estado
 *      operativo de un refrigerador de medicamentos (HU-01 escenario 3).
 *   5. Rango físico −55..125 °C.
 *
 * La resolución se toma del byte de configuración (bits R1:R0) y se enmascaran
 * los bits indefinidos, como exige la hoja de datos para 9–11 bits.
 */
LecturaSensor decodificarScratchpadDS18B20(const uint8_t scratchpad[9]);

/// Resultado de una medición SHT31.
struct LecturaSHT31 {
    LecturaSensor temperatura;
};

/**
 * Decodifica la palabra de temperatura de la trama del SHT31 (T_msb, T_lsb,
 * T_crc, ...). T = −45 + 175·S/65535.
 */
LecturaSHT31 decodificarSHT31(const uint8_t trama[6]);

/// Marca la lectura del SHT31 con un fallo (p. ej. sin ACK I²C).
LecturaSHT31 falloSHT31(FalloSensor fallo);

}  // namespace core

#endif  // CORE_SENSORES_H
