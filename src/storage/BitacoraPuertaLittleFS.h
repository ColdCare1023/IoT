#ifndef BITACORA_PUERTA_LITTLEFS_H
#define BITACORA_PUERTA_LITTLEFS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../core/BitacoraPuerta.h"

/**
 * Bitácora de puerta en LittleFS (HU-04 esc. 1, HU-35): `/tt/puerta.bin`.
 *
 * La política (capacidad, descarte contado, confirmación por id) es
 * `core::BitacoraPuerta`, probada en el host. Esta clase aporta el archivo y
 * el cerrojo: anota la tarea de captura y publica/confirma la de red.
 * `begin()` va DESPUÉS de montar LittleFS (BufferLittleFS::begin).
 */
class BitacoraPuertaLittleFS {
public:
    BitacoraPuertaLittleFS();

    /// false si LittleFS no está montado: el llamador usa la cola en RAM.
    bool begin();
    bool disponible() const { return _disponible; }

    core::ResultadoAnotacion anotar(bool abierta, uint32_t bootId, uint32_t uptimeS, uint32_t epoch);
    bool primero(core::EventoPuerta& evento);
    bool confirmar(uint32_t id);
    uint32_t pendientes();
    uint32_t descartados();
    uint32_t corrupciones();

private:
    class Archivo : public core::AlmacenBitacora {
    public:
        bool leer(std::vector<uint8_t>& datos) override;
        bool escribir(const std::vector<uint8_t>& datos) override;
    };

    SemaphoreHandle_t _mutex;
    Archivo _archivo;
    core::BitacoraPuerta _bitacora;
    bool _disponible = false;
};

#endif  // BITACORA_PUERTA_LITTLEFS_H
