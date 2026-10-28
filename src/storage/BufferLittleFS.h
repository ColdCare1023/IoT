#ifndef BUFFER_LITTLEFS_H
#define BUFFER_LITTLEFS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../core/ColaLecturas.h"

/**
 * Buffer offline sobre LittleFS (HU-06/HU-07).
 *
 * Esta clase aporta SOLO dos cosas: el acceso real a flash y la exclusión
 * mutua entre la tarea de captura (escribe cada intervalo) y la de red (lee,
 * confirma y libera). Toda la política —FIFO, 80 %, saturación sin
 * sobrescribir, cuarentena, recuperación tras corte— es `core::ColaLecturas`,
 * probada en el host.
 *
 * Contador de pérdidas en NVS, no en LittleFS: se escribe justamente cuando
 * LittleFS está lleno.
 */
class BufferLittleFS {
public:
    BufferLittleFS();

    /// Monta LittleFS. Si la partición no se puede montar se formatea (sin
    /// ello el nodo no podría guardar nada) y `formateado` lo indica: se
    /// reporta como evento, nunca en silencio.
    bool begin(bool& formateado, core::InformeMontaje& informe);

    core::ResultadoGuardado guardar(const core::RegistroLectura& registro);

    /// Drena con el mutex tomado SOLO alrededor de cada operación de cola.
    core::ResumenDrenaje drenar(core::Transmisor& transmisor, int maxPorCiclo);

    /// ADR-18 (`buffer descartar`): descarta los pendientes que había al
    /// llamarla, por tandas de segmentos con el mutex tomado solo en cada una
    /// (borrar 300 archivos de una vez pasaría el watchdog de 5 s y frenaría la
    /// captura). `entreTandas` se llama entre tandas (watchdog). Llamar desde
    /// la tarea de red, nunca durante `drenar()`.
    uint32_t descartarPendientes(void (*entreTandas)());

    uint32_t pendientes();
    uint8_t ocupacionPct();
    uint32_t capacidad();
    uint32_t cuarentenados();
    uint32_t cuarentenaDesbordada();
    uint32_t erroresEscritura();
    core::EstadoPerdidas perdidas();
    size_t bytesLibres();
    size_t bytesTotales();  ///< tamaño útil de LittleFS (B medido en el nodo)

    bool consumirAvisoOcupacion(uint8_t& pct);
    bool consumirAvisoPerdidas(core::EstadoPerdidas& estado);

private:
    class AlmacenLittleFS : public core::AlmacenSegmentos {
    public:
        std::vector<uint32_t> listarSegmentos() override;
        size_t tamanoSegmento(uint32_t id) override;
        size_t anexar(uint32_t id, const uint8_t* datos, size_t longitud) override;
        bool leer(uint32_t id, size_t desplazamiento, uint8_t* datos, size_t longitud) override;
        bool borrarSegmento(uint32_t id) override;
        bool leerMeta(const char* nombre, uint8_t* datos, size_t longitud) override;
        bool escribirMeta(const char* nombre, const uint8_t* datos, size_t longitud) override;
        size_t tamanoCuarentena() override;
        size_t anexarCuarentena(const uint8_t* datos, size_t longitud) override;
        size_t bytesLibres() override;
    };

    class PerdidasNVS : public core::PersistenciaPerdidas {
    public:
        bool cargar(core::EstadoPerdidas& estado) override;
        bool guardar(const core::EstadoPerdidas& estado) override;
    };

    class CerrojoFreeRTOS : public core::Cerrojo {
    public:
        explicit CerrojoFreeRTOS(SemaphoreHandle_t m) : _m(m) {}
        void tomar() override { xSemaphoreTakeRecursive(_m, portMAX_DELAY); }
        void soltar() override { xSemaphoreGiveRecursive(_m); }

    private:
        SemaphoreHandle_t _m;
    };

    SemaphoreHandle_t _mutex;
    CerrojoFreeRTOS _cerrojo;
    AlmacenLittleFS _almacen;
    PerdidasNVS _perdidas;
    core::ColaLecturas _cola;
};

#endif  // BUFFER_LITTLEFS_H
