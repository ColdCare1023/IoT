#ifndef CORE_TIEMPO_GUARDADO_H
#define CORE_TIEMPO_GUARDADO_H

#include <cstdint>

namespace core {

/**
 * Estadística del tiempo captura → lectura confirmada en LittleFS (BUF-01, RNF-01).
 *
 * La tarea de captura mide con `esp_timer` desde antes de leer los sensores hasta
 * que `BufferLittleFS::guardar()` cierra el archivo (LittleFS confirma los datos
 * al cerrar). Las dos marcas usan el mismo reloj del ESP32, así que la medición no
 * depende del reloj del servidor ni de SNTP.
 *
 * Solo la escribe la tarea de captura; la consola la lee para `estado`. Los campos
 * son de 32 bits (lectura atómica en el ESP32); una lectura a mitad de `registrar()`
 * puede mezclar dos capturas, lo que no importa para un informe.
 */
struct TiempoGuardado {
    static constexpr uint32_t LIMITE_MS = 5000;  // RNF-01: ≤ 5 s

    uint32_t n = 0;           // lecturas guardadas medidas
    uint32_t fallidas = 0;    // capturas que no se pudieron guardar (no entran en n)
    uint32_t ultimoMs = 0;
    uint32_t minimoMs = UINT32_MAX;
    uint32_t maximoMs = 0;
    uint32_t sobreLimite = 0;  // guardadas en más de LIMITE_MS
    uint64_t sumaMs = 0;
    // Histograma: < 1 s, 1–2 s, 2–5 s, > 5 s.
    uint32_t tramos[4] = {0, 0, 0, 0};

    void registrar(uint32_t ms, bool guardada) {
        if (!guardada) {
            fallidas++;
            return;
        }
        n++;
        ultimoMs = ms;
        if (ms < minimoMs) minimoMs = ms;
        if (ms > maximoMs) maximoMs = ms;
        sumaMs += ms;
        if (ms > LIMITE_MS) sobreLimite++;
        tramos[ms < 1000 ? 0 : ms < 2000 ? 1 : ms <= LIMITE_MS ? 2 : 3]++;
    }

    uint32_t mediaMs() const { return n ? (uint32_t)(sumaMs / n) : 0; }
};

}  // namespace core

#endif
