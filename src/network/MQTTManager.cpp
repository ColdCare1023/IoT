#include "MQTTManager.h"

#include <LittleFS.h>

#include "ca_embebida.h"

#include "../core/Credenciales.h"
#include "../core/Payload.h"
#include "../system/Watchdog.h"

static_assert(MQTT_PORT == 8883, "HU-14: solo mqtts (TLS) en 8883; no hay fallback a 1883.");

MQTTManager::MQTTManager(WiFiClientSecure& tls) : _tls(tls) {}

bool MQTTManager::_cargarCA() {
#if CA_EMBEBIDA
    // IOT-001: la CA va dentro de la imagen (firmada con Secure Boot). La de
    // LittleFS se ignora: un cambio en esa partición ya no redirige el nodo.
    _tls.setCACert(ROOT_CA_PEM);
    LOG_I("MQTT", "CA del broker embebida en el firmware.");
    return true;
#else
    LOG_E("MQTT", "CA leida de LittleFS (reemplazable): en produccion debe embeberse (IOT-001).");
#endif
    if (!LittleFS.exists(CERT_CA_ARCHIVO)) {
        LOG_E("MQTT", "Falta %s. Subir data/certs con 'pio run -t uploadfs' (ANTES de instalar: "
                      "borra el buffer).", CERT_CA_ARCHIVO);
        return false;
    }
    File f = LittleFS.open(CERT_CA_ARCHIVO, "r");
    if (!f) return false;
    _caPem = f.readString();
    f.close();
    if (_caPem.indexOf("-----BEGIN CERTIFICATE-----") < 0) {
        LOG_E("MQTT", "%s no contiene un certificado PEM.", CERT_CA_ARCHIVO);
        _caPem = String();
        return false;
    }
    _tls.setCACert(_caPem.c_str());
    return true;
}

void MQTTManager::configurar(const String& host, const String& deviceId, const String& token) {
    _host = host;
    _deviceId = deviceId;
    _token = token;
    _topicoLecturas = String(TOPICO_PREFIJO) + deviceId + "/lecturas";
    _topicoEventos = String(TOPICO_PREFIJO) + deviceId + "/eventos";
    _topicoAck = String(TOPICO_PREFIJO) + deviceId + "/ack";
    _lwt = core::serializarLWT(std::string(deviceId.c_str()));

    // HU-14: un host con esquema o puerto ("mqtt://…", "…:1883") se rechaza.
    _aprovisionado = core::hostMqttValido(std::string(host.c_str())) &&
                     core::credencialValida(std::string(token.c_str()));
    if (!core::hostMqttValido(std::string(host.c_str())) && !host.isEmpty()) {
        LOG_E("MQTT", "mqtt_host invalido: solo nombre de host, sin esquema ni puerto (TLS 8883 fijo).");
    }

    // TLS: mbedTLS 3 (ESP-IDF 5.5) negocia TLS 1.2; no existe 1.0/1.1 al que
    // degradar. La garantía que depende de nosotros es autenticar al
    // servidor: CA + verificación del nombre (el host viaja en el SNI).
    _tls.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_MS / 1000);
    _tls.setTimeout(TLS_HANDSHAKE_TIMEOUT_MS / 1000);
    _caCargada = _cargarCA();
    if (!_caCargada) LOG_E("MQTT", "Sin CA valida: NO se conectara (las lecturas siguen en LittleFS).");
    if (!_aprovisionado) LOG_E("MQTT", "Sin host o token MQTT: nodo no aprovisionado.");

    _mqtt.begin(_host.c_str(), MQTT_PORT, _tls);
    _mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
    _mqtt.setCleanSession(true);
    _mqtt.setTimeout(MQTT_COMMAND_TIMEOUT_MS);
    _mqtt.setWill(_topicoEventos.c_str(), _lwt.c_str(), /*retained=*/false, MQTT_QOS);
    _mqtt.onMessage([this](String& topico, String& payload) { _alRecibir(topico, payload); });
}

bool MQTTManager::conectar() {
    if (_mqtt.connected()) return true;
    if (!_caCargada || !_aprovisionado) return false;  // fallo cerrado

    // Enfriamiento mínimo entre intentos: el handshake TLS es caro (RAM y
    // tiempo) y un broker que rechaza credenciales no cambia de opinión en 1 s.
    const uint32_t ahora = millis();
    if (_ultimoIntentoMs != 0 && !_reintento.debeIntentar(ahora, _ultimoIntentoMs)) return false;
    _ultimoIntentoMs = ahora;

    LOG_I("MQTT", "Conectando a %s:%d como '%s'...", _host.c_str(), MQTT_PORT, _deviceId.c_str());
    if (!_mqtt.connect(_deviceId.c_str(), _deviceId.c_str(), _token.c_str())) {
        const int rc = (int)_mqtt.returnCode();
        _reintento.registrarFallo();
        LOG_E("MQTT", "Conexion rechazada: lastError=%d returnCode=%d.", (int)_mqtt.lastError(), rc);
        if (rc == LWMQTT_BAD_USERNAME_OR_PASSWORD || rc == LWMQTT_NOT_AUTHORIZED) {
            LOG_E("MQTT", "  -> credencial invalida o revocada (HU-10/HU-44).");
        } else if (rc == 0) {
            LOG_E("MQTT", "  -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).");
        }
        return false;
    }
    _reintento.registrarExito();
    if (!_mqtt.subscribe(_topicoAck.c_str(), MQTT_QOS)) {
        LOG_E("MQTT", "No se pudo suscribir a %s: el buffer no se liberara.", _topicoAck.c_str());
    }
    LOG_I("MQTT", "Conectado (TLS, QoS %d). Acuses en %s.", MQTT_QOS, _topicoAck.c_str());
    return true;
}

bool MQTTManager::conectado() { return _mqtt.connected(); }

void MQTTManager::loop() { _mqtt.loop(); }

void MQTTManager::desconectarOrdenadamente() {
    if (_mqtt.connected()) _mqtt.disconnect();
}

void MQTTManager::actualizarToken(const String& token) {
    _token = token;
    _reintento.registrarExito();  // credencial nueva: se reintenta de inmediato
    _ultimoIntentoMs = 0;
    _aprovisionado = core::hostMqttValido(std::string(_host.c_str())) &&
                     core::credencialValida(std::string(token.c_str()));
}

MQTTManager::Publicacion MQTTManager::publicar(const String& topico, const std::string& payload) {
    if (!_mqtt.connected()) return Publicacion::SinConexion;
    // QoS 1: true SOLO con el PUBACK del broker. El PUBACK NO autoriza a
    // borrar nada del buffer; eso lo decide el acuse lógico (ver Ack.h).
    const bool ok = _mqtt.publish(topico.c_str(), payload.c_str(), (int)payload.size(),
                                  /*retained=*/false, MQTT_QOS);
    if (ok) return Publicacion::Confirmada;
    LOG_E("MQTT", "Sin PUBACK (lastError=%d).", (int)_mqtt.lastError());
    return _mqtt.connected() ? Publicacion::SinPuback : Publicacion::SinConexion;
}

bool MQTTManager::esperarAck(uint32_t bootId, uint32_t seqNo, uint32_t timeoutMs,
                             core::EstadoAck& estado, std::string& huella) {
    const uint32_t inicio = millis();
    for (;;) {
        // Puede haber llegado ya durante el publish() (lwmqtt procesa
        // entrantes mientras espera el PUBACK).
        if (_acks.buscar(bootId, seqNo, estado, huella)) {
            _acks.olvidar(bootId, seqNo);
            return true;
        }
        if (millis() - inicio >= timeoutMs || !_mqtt.connected()) return false;
        _mqtt.loop();
        alimentarWatchdog();
        delay(10);
    }
}

void MQTTManager::_alRecibir(String& topico, String& payload) {
    if (topico != _topicoAck) return;
    core::AckLogico ack;
    if (!core::parsearAck(std::string(payload.c_str()), ack) ||
        ack.deviceId != std::string(_deviceId.c_str())) {
        _acksIgnorados++;  // un acuse ambiguo o ajeno nunca libera nada
        return;
    }
    _acks.anotar(ack.bootId, ack.seqNo, ack.estado, ack.contenidoSha256);
}
