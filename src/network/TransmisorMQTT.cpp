#include "TransmisorMQTT.h"

#include "../config.h"
#include "../core/Payload.h"
#include "../system/Reloj.h"
#include "../system/Watchdog.h"

TransmisorMQTT::TransmisorMQTT(MQTTManager& mqtt, const String& deviceId)
    : _mqtt(mqtt), _deviceId(deviceId.c_str()) {}

core::ResultadoEnvio TransmisorMQTT::_desdeAck(core::EstadoAck estado) {
    if (core::ackAutorizaLiberar(estado)) return core::ResultadoEnvio::Confirmado;
    if (estado == core::EstadoAck::Rechazado) return core::ResultadoEnvio::Rechazado;
    return core::ResultadoEnvio::Fallo;
}

core::ResultadoEnvio TransmisorMQTT::enviar(const core::RegistroLectura& r) {
    core::EstadoAck estado;
    if (_mqtt.acks().buscar(r.bootId, r.seqNo, estado)) {
        _mqtt.acks().olvidar(r.bootId, r.seqNo);
        return _desdeAck(estado);
    }

    const std::string json = core::serializarTelemetria(r, _deviceId, baseTiempo());
    if (json.empty()) {
        std::string motivo;
        core::validarRegistro(r, motivo);
        LOG_E("Tx", "Registro boot=%lu seq=%lu no cumple el contrato (%s): a cuarentena.",
              (unsigned long)r.bootId, (unsigned long)r.seqNo, motivo.c_str());
        return core::ResultadoEnvio::Invalido;
    }

    alimentarWatchdog();
    switch (_mqtt.publicar(_mqtt.topicoLecturas(), json)) {
        case MQTTManager::Publicacion::SinConexion: return core::ResultadoEnvio::SinConexion;
        case MQTTManager::Publicacion::SinPuback:   return core::ResultadoEnvio::Fallo;
        case MQTTManager::Publicacion::Confirmada:  break;
    }

    // PUBACK recibido = el BROKER tiene el mensaje. Todavía no se libera nada.
    if (!_mqtt.esperarAck(r.bootId, r.seqNo, ACK_LOGICO_TIMEOUT_MS, estado)) {
        _pubacksSinAck++;
        LOG_E("Tx", "PUBACK si, acuse logico no (boot=%lu seq=%lu): se conserva en LittleFS.",
              (unsigned long)r.bootId, (unsigned long)r.seqNo);
        return core::ResultadoEnvio::Fallo;
    }
    return _desdeAck(estado);
}
