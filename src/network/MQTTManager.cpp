#include "MQTTManager.h"

#include <LittleFS.h>

#include "../core/Credenciales.h"

MQTTManager::MQTTManager(WiFiClientSecure& tls) : _tls(tls) {}

bool MQTTManager::_cargarCA() {
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

    _aprovisionado = core::credencialValida(std::string(host.c_str())) &&
                     core::credencialValida(std::string(token.c_str()));

    // TLS: mbedTLS del ESP-IDF 4.4 negocia TLS 1.2 (no hay 1.0/1.1 al que
    // degradar). La garantía que depende de nosotros es autenticar al
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
}

bool MQTTManager::conectar() {
    if (_mqtt.connected()) return true;
    if (!_caCargada || !_aprovisionado) return false;  // fallo cerrado

    // Enfriamiento mínimo entre intentos: el handshake TLS es caro (RAM y
    // tiempo) y un broker que rechaza credenciales no cambia de opinión en 1 s.
    const uint32_t ahora = millis();
    if (_ultimoIntentoMs != 0 && ahora - _ultimoIntentoMs < 5000) return false;
    _ultimoIntentoMs = ahora;

    LOG_I("MQTT", "Conectando a %s:%d como '%s'...", _host.c_str(), MQTT_PORT, _deviceId.c_str());
    if (!_mqtt.connect(_deviceId.c_str(), _deviceId.c_str(), _token.c_str())) {
        const int rc = (int)_mqtt.returnCode();
        LOG_E("MQTT", "Conexion rechazada: lastError=%d returnCode=%d.", (int)_mqtt.lastError(), rc);
        if (rc == LWMQTT_BAD_USERNAME_OR_PASSWORD || rc == LWMQTT_NOT_AUTHORIZED) {
            LOG_E("MQTT", "  -> credencial invalida o revocada.");
        } else if (rc == 0) {
            LOG_E("MQTT", "  -> sin CONNACK: TLS rechazado (CA/hostname) o red caida (HU-09).");
        }
        return false;
    }
    LOG_I("MQTT", "Conectado (TLS, QoS %d).", MQTT_QOS);
    return true;
}

bool MQTTManager::conectado() { return _mqtt.connected(); }

void MQTTManager::loop() { _mqtt.loop(); }

MQTTManager::Publicacion MQTTManager::publicar(const String& topico, const std::string& payload) {
    if (!_mqtt.connected()) return Publicacion::SinConexion;
    // QoS 1: true SOLO con el PUBACK del broker.
    const bool ok = _mqtt.publish(topico.c_str(), payload.c_str(), (int)payload.size(),
                                  /*retained=*/false, MQTT_QOS);
    if (ok) return Publicacion::Confirmada;
    LOG_E("MQTT", "Sin PUBACK (lastError=%d).", (int)_mqtt.lastError());
    return _mqtt.connected() ? Publicacion::SinPuback : Publicacion::SinConexion;
}
