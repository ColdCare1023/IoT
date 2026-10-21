#include "TransmisorMQTT.h"

#include "../config.h"
#include "../core/Payload.h"
#include "../core/Sha256.h"
#include "../system/Reloj.h"
#include "../system/Watchdog.h"

TransmisorMQTT::TransmisorMQTT(MQTTManager& mqtt, const String& deviceId)
    : _mqtt(mqtt), _deviceId(deviceId.c_str()) {}

core::ResultadoEnvio TransmisorMQTT::_desdeAck(const core::RegistroLectura& r, core::EstadoAck estado,
                                               const std::string& huella) {
    const bool coincide = _envios.coincide(r.bootId, r.seqNo, huella);
    const core::ResultadoEnvio resultado = core::resultadoDeAck(estado, coincide);
    if (core::ackAutorizaLiberar(estado) && !coincide) {
        _acksConHuellaAjena++;
        LOG_E("Tx", "Acuse boot=%lu seq=%lu con huella que no corresponde a lo publicado: a cuarentena "
                    "(no se borra).", (unsigned long)r.bootId, (unsigned long)r.seqNo);
    }
    return resultado;
}

core::ResultadoEnvio TransmisorMQTT::enviar(const core::RegistroLectura& r) {
    core::EstadoAck estado;
    std::string huella;
    if (_mqtt.acks().buscar(r.bootId, r.seqNo, estado, huella)) {
        _mqtt.acks().olvidar(r.bootId, r.seqNo);
        return _desdeAck(r, estado, huella);
    }

    const std::string json = core::serializarTelemetria(r, _deviceId, baseTiempo());
    if (json.empty()) {
        std::string motivo;
        core::validarRegistro(r, motivo);
        LOG_E("Tx", "Registro boot=%lu seq=%lu no cumple el contrato (%s): a cuarentena.",
              (unsigned long)r.bootId, (unsigned long)r.seqNo, motivo.c_str());
        return core::ResultadoEnvio::Invalido;
    }

    // IOT-008: la huella se anota ANTES de publicar: el acuse puede llegar
    // mientras lwmqtt espera el PUBACK.
    _envios.anotar(r.bootId, r.seqNo, core::sha256Hex(json));
    alimentarWatchdog();
    switch (_mqtt.publicar(_mqtt.topicoLecturas(), json)) {
        case MQTTManager::Publicacion::SinConexion: return core::ResultadoEnvio::SinConexion;
        case MQTTManager::Publicacion::SinPuback:   return core::ResultadoEnvio::Fallo;
        case MQTTManager::Publicacion::Confirmada:  break;
    }

    // PUBACK recibido = el BROKER tiene el mensaje. Todavía no se libera nada.
    if (!_mqtt.esperarAck(r.bootId, r.seqNo, ACK_LOGICO_TIMEOUT_MS, estado, huella)) {
        _pubacksSinAck++;
        LOG_E("Tx", "PUBACK si, acuse logico no (boot=%lu seq=%lu): se conserva en LittleFS.",
              (unsigned long)r.bootId, (unsigned long)r.seqNo);
        return core::ResultadoEnvio::Fallo;
    }
    return _desdeAck(r, estado, huella);
}
