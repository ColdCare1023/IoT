#ifndef TRANSMISOR_MQTT_H
#define TRANSMISOR_MQTT_H

#include <Arduino.h>

#include "../core/ColaLecturas.h"
#include "MQTTManager.h"

/**
 * Adaptador entre la política de drenaje (`core::drenar`) y MQTT.
 *
 * Secuencia por registro (HU-07, HU-11):
 *   1. ¿Ya llegó un acuse tardío para este (boot_id, seq_no)? → se resuelve
 *      sin volver a publicar.
 *   2. Serializa el contrato v1.0. Si no es válido → Invalido (cuarentena,
 *      NUNCA se publica una estructura inválida — HU-05 esc. 3).
 *   3. Publica QoS 1 y espera el PUBACK.            ← NO libera nada
 *   4. Espera el acuse lógico del backend.          ← esto sí
 *        committed / duplicate → Confirmado (se libera)
 *        rejected              → Rechazado (cuarentena)
 *        timeout               → Fallo (se conserva; el reenvío es idempotente)
 */
class TransmisorMQTT : public core::Transmisor {
public:
    TransmisorMQTT(MQTTManager& mqtt, const String& deviceId);
    core::ResultadoEnvio enviar(const core::RegistroLectura& registro) override;

    uint32_t pubacksSinAck() const { return _pubacksSinAck; }

private:
    static core::ResultadoEnvio _desdeAck(core::EstadoAck estado);
    MQTTManager& _mqtt;
    std::string _deviceId;
    uint32_t _pubacksSinAck = 0;
};

#endif  // TRANSMISOR_MQTT_H
