#ifndef CORE_REGISTRO_H
#define CORE_REGISTRO_H

#include <cstddef>
#include <cstdint>

#include "Sensores.h"

namespace core {

/**
 * Registro binario de una lectura, tal como se guarda en LittleFS.
 *
 * POR QUÉ BINARIO Y NO EL JSON
 * ----------------------------
 * El buffer offline debe aguantar desconexiones largas en una partición de
 * 1,6 MB. Un JSON por archivo ocupa ~500 B y, en LittleFS, cada archivo
 * pequeño cuesta metadatos propios: la capacidad real quedaba en unas pocas
 * horas. Un registro de 40 B con CRC32 da ~30 000 lecturas (≈10 días a 30 s)
 * y hace trivial detectar un registro corrupto tras un corte de energía.
 *
 * El JSON se genera AL PUBLICAR, de forma determinista desde el registro, así
 * que `boot_id`, `seq_no`, `captured_at` y los valores quedan intocables
 * desde la captura (lo que exige HU-06 escenario 1).
 *
 * Formato (little-endian, 40 bytes):
 *   0-1   magia 'T''R'          20-21 temp DS18B20 (centésimas °C, int16)
 *   2     versión de formato     22-23 temp SHT31 (centésimas °C, int16)
 *   3     banderas               24-25 HR SHT31 (centésimas %, uint16)
 *   4-7   boot_id                26    fallo DS18B20
 *   8-11  seq_no                 27    fallo SHT31 temperatura
 *   12-15 época UTC de captura   28    fallo SHT31 humedad
 *         (0 = sin sincronía)    29-31 versión de firmware (mayor.menor.parche)
 *   16-19 uptime en segundos     32-33 segundos de puerta abierta en la ventana
 *                                34-35 racha de apertura en curso (s)
 *                                36-39 CRC32 de los bytes 0..35
 */

static const size_t TAM_REGISTRO = 40;
static const uint8_t VERSION_FORMATO_REGISTRO = 1;

static const int16_t NULO_I16 = INT16_MIN;     ///< centésimas "sin valor"
static const uint16_t NULO_U16 = 0xFFFF;

// Banderas (byte 3).
static const uint8_t BANDERA_TIEMPO_SNTP     = 0x01;  ///< época válida por SNTP en este arranque
static const uint8_t BANDERA_MC38_INSTALADO  = 0x02;
static const uint8_t BANDERA_PUERTA_ABIERTA  = 0x04;  ///< hubo apertura en la ventana
static const uint8_t BANDERA_SIMULADO        = 0x08;  ///< lectura inyectada en modo demo
static const uint8_t BANDERA_SENSADO_ENERGIA = 0x10;  ///< hay sensado de suministro instalado
static const uint8_t BANDERA_EN_RESPALDO     = 0x20;  ///< alimentado por el Mini UPS al capturar

struct RegistroLectura {
    uint32_t bootId = 0;
    uint32_t seqNo = 0;
    uint32_t capturadoEpoch = 0;
    uint32_t uptimeS = 0;
    int16_t tempDsCenti = NULO_I16;
    int16_t tempShtCenti = NULO_I16;
    uint16_t humShtCenti = NULO_U16;
    FalloSensor falloDs = FalloSensor::SinRespuesta;
    FalloSensor falloShtT = FalloSensor::SinRespuesta;
    FalloSensor falloShtH = FalloSensor::SinRespuesta;
    uint8_t banderas = 0;
    uint8_t fwMayor = 0;
    uint8_t fwMenor = 0;
    uint8_t fwParche = 0;
    uint16_t puertaAbiertaS = 0;
    uint16_t puertaRachaS = 0;

    bool tiene(uint8_t bandera) const { return (banderas & bandera) != 0; }
    void poner(uint8_t bandera, bool activa) {
        banderas = activa ? (uint8_t)(banderas | bandera) : (uint8_t)(banderas & ~bandera);
    }
};

/// CRC-32 IEEE 802.3 (el de zlib). CRC("123456789") = 0xCBF43926.
uint32_t crc32(const uint8_t* datos, size_t longitud);

void codificarRegistro(const RegistroLectura& r, uint8_t salida[TAM_REGISTRO]);

/// Devuelve false si la magia, la versión o el CRC no coinciden: el registro
/// está corrupto y NO debe publicarse.
bool decodificarRegistro(const uint8_t entrada[TAM_REGISTRO], RegistroLectura& r);

/// °C/% → centésimas, redondeando. Valores no finitos o fuera de rango de
/// int16 → NULO. La conversión inversa devuelve NaN para NULO.
int16_t aCentesimas(float valor);
uint16_t aCentesimasSinSigno(float valor);
float deCentesimas(int16_t centesimas);
float deCentesimasSinSigno(uint16_t centesimas);

/// Rellena temperatura/fallo del DS18B20 desde una lectura validada.
void fijarDS18B20(RegistroLectura& r, const LecturaSensor& lectura);
/// Rellena temperatura y humedad del SHT31.
void fijarSHT31(RegistroLectura& r, const LecturaSHT31& lectura);

}  // namespace core

#endif  // CORE_REGISTRO_H
