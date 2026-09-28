#ifndef CORE_ACK_H
#define CORE_ACK_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

/**
 * Acuse LÓGICO del backend (HU-07) — distinto del PUBACK de MQTT.
 *
 * REGLA DE ORO DEL BUFFER
 * -----------------------
 * El PUBACK de QoS 1 solo dice que el BROKER recibió el paquete. No dice que
 * FastAPI lo validó ni que PostgreSQL hizo COMMIT. Por eso el registro local
 * NO se libera con el PUBACK: se libera solo cuando llega, por
 * `farmacias/<device_id>/ack`, un acuse con el mismo (boot_id, seq_no) y un
 * estado que implique persistencia:
 *
 *   {"device_id":"ESP32_FARMACIA_01","boot_id":42,"seq_no":1205,"status":"committed"}
 *
 *   committed  → COMMIT en PostgreSQL                → se libera
 *   duplicate  → ya existía (idempotencia HU-11)     → se libera
 *   rejected   → rechazo PERMANENTE ya auditado      → NO se borra: pasa a
 *                cuarentena en LittleFS y la cola sigue (si no, una lectura
 *                inválida bloquearía para siempre el FIFO detrás de ella).
 *
 * Por compatibilidad se aceptan también `estado` y los valores en español
 * (`commit_confirmado`, `duplicado`, `rechazado`).
 */

enum class EstadoAck : uint8_t { Confirmado, Duplicado, Rechazado, Desconocido };

struct AckLogico {
    std::string deviceId;
    uint32_t bootId = 0;
    uint32_t seqNo = 0;
    EstadoAck estado = EstadoAck::Desconocido;
};

/// Parsea el cuerpo del acuse. Devuelve false si falta algún campo o el
/// estado no se reconoce (un acuse ambiguo nunca autoriza un borrado).
bool parsearAck(const std::string& json, AckLogico& ack);

/// ¿El estado implica que la lectura quedó persistida en PostgreSQL?
inline bool ackAutorizaLiberar(EstadoAck estado) {
    return estado == EstadoAck::Confirmado || estado == EstadoAck::Duplicado;
}

/**
 * Memoria corta de acuses recibidos, indexada por (boot_id, seq_no).
 *
 * Cubre dos carreras reales:
 *   - el acuse llega DURANTE el `publish()` (lwmqtt procesa entrantes
 *     mientras espera el PUBACK), antes de que el nodo empiece a esperarlo;
 *   - el acuse llega TARDE, después del timeout: en el siguiente ciclo el
 *     registro se libera sin volver a publicarlo.
 */
class MemoriaAcks {
public:
    static const size_t CAPACIDAD = 32;

    void anotar(uint32_t bootId, uint32_t seqNo, EstadoAck estado);
    bool buscar(uint32_t bootId, uint32_t seqNo, EstadoAck& estado) const;
    void olvidar(uint32_t bootId, uint32_t seqNo);
    void limpiar();

private:
    struct Entrada {
        uint32_t bootId = 0;
        uint32_t seqNo = 0;
        EstadoAck estado = EstadoAck::Desconocido;
        bool usada = false;
    };
    Entrada _entradas[CAPACIDAD];
    size_t _siguiente = 0;
};

}  // namespace core

#endif  // CORE_ACK_H
