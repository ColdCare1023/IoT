#include "MQTTManager.h"

#include <LittleFS.h>

#include "ca_embebida.h"

#include "../core/Certificado.h"
#include "../core/Credenciales.h"
#include "../core/Payload.h"
#include "../core/PlazosConexion.h"
#include "../system/Reloj.h"
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

void MQTTManager::configurar(const String& host, const String& deviceId, const String& token,
                             uint32_t bootId) {
    _host = host;
    _deviceId = deviceId;
    _token = token;
    _topicoLecturas = String(TOPICO_PREFIJO) + deviceId + "/lecturas";
    _topicoEventos = String(TOPICO_PREFIJO) + deviceId + "/eventos";
    _topicoAck = String(TOPICO_PREFIJO) + deviceId + "/ack";
    _topicoOta = String(TOPICO_PREFIJO) + deviceId + "/ota";
    _lwt = core::serializarLWT(std::string(deviceId.c_str()), bootId);

    // HU-14: un host con esquema o puerto ("mqtt://…", "…:1883") se rechaza.
    _aprovisionado = core::hostMqttValido(std::string(host.c_str())) &&
                     core::credencialValida(std::string(token.c_str()));
    if (!core::hostMqttValido(std::string(host.c_str())) && !host.isEmpty()) {
        LOG_E("MQTT", "mqtt_host invalido: solo nombre de host, sin esquema ni puerto (TLS 8883 fijo).");
    }

    // TLS: mbedTLS 3 (ESP-IDF 5.5) negocia TLS 1.2; no existe 1.0/1.1 al que
    // degradar. La garantía que depende de nosotros es autenticar al
    // servidor: CA + verificación del nombre (el host viaja en el SNI).
    // H1: cada plazo en la unidad que espera su API (ver core/PlazosConexion.h).
    // setHandshakeTimeout va en SEGUNDOS; los otros dos, en MILISEGUNDOS.
    _tls.setHandshakeTimeout(core::PLAZO_HANDSHAKE_TLS_S);
    // Conexión TCP y SO_RCVTIMEO/SO_SNDTIMEO del socket: los gobierna
    // NetworkClient::_timeout, que solo cambia setConnectionTimeout(). Sin esta
    // llamada valen 30 s y connect() puede agotar el watchdog (bucle TASK_WDT con
    // un mqtt_host inalcanzable, verificado en el banco el 21/11/2026).
    _tls.setConnectionTimeout(core::PLAZO_CLIENTE_TLS_MS);
    // Stream::setTimeout: plazo de las lecturas "timed" de Stream (otro _timeout).
    _tls.setTimeout(core::PLAZO_CLIENTE_TLS_MS);
    _caCargada = _cargarCA();
    if (!_caCargada) LOG_E("MQTT", "Sin CA valida: NO se conectara (las lecturas siguen en LittleFS).");
    if (!_aprovisionado) LOG_E("MQTT", "Sin host o token MQTT: nodo no aprovisionado.");

    _mqtt.begin(_host.c_str(), MQTT_PORT, _tls);
    _mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
    _mqtt.setCleanSession(true);
    _mqtt.setTimeout(core::PLAZO_COMANDO_MQTT_MS);  // milisegundos
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
    // HU-09 esc. 2: primero solo TLS (CA + nombre del servidor). El CONNECT, que
    // lleva el token, sale recién cuando la vigencia del certificado se comprobó.
    _tls.stop();
    if (!_tls.connect(_host.c_str(), MQTT_PORT)) {
        _reintento.registrarFallo();
        _fallosTotales++;
        LOG_E("MQTT", "Conexion rechazada: TLS sin establecer.");
        LOG_E("MQTT", "  -> TLS rechazado (CA/hostname), timeout del handshake o red caida (HU-09).");
        return false;
    }
    alimentarWatchdog();
    const core::VigenciaCertificado vigencia = _vigenciaBroker();
    if (core::vigenciaRechaza(vigencia)) {
        _tls.stop();
        _reintento.registrarFallo();
        _fallosTotales++;
        LOG_E("MQTT", "Certificado del broker %s: conexion rechazada sin enviar credenciales (HU-09).",
              core::nombreVigencia(vigencia));
        return false;
    }
    _vigenciaPendiente = vigencia == core::VigenciaCertificado::SinHora;
    if (_vigenciaPendiente) LOG_I("MQTT", "Sin hora SNTP: la vigencia del certificado se comprueba al llegar.");
    if (!_mqtt.connect(_deviceId.c_str(), _deviceId.c_str(), _token.c_str(), /*skip=*/true)) {
        const int rc = (int)_mqtt.returnCode();
        _reintento.registrarFallo();
        _fallosTotales++;
        LOG_E("MQTT", "Conexion rechazada: lastError=%d returnCode=%d.", (int)_mqtt.lastError(), rc);
        if (rc == LWMQTT_BAD_USERNAME_OR_PASSWORD || rc == LWMQTT_NOT_AUTHORIZED) {
            LOG_E("MQTT", "  -> credencial invalida o revocada (HU-10/HU-44).");
        } else if (rc == 0) {
            LOG_E("MQTT", "  -> sin CONNACK: el broker no respondio al CONNECT o la red cayo.");
        }
        return false;
    }
    _reintento.registrarExito();
    // El CONNECT agotó su tramo; cada SUBACK puede esperar otro plazo de
    // comando: se alimenta el watchdog entre llamadas (H1).
    alimentarWatchdog();
    if (!_mqtt.subscribe(_topicoAck.c_str(), MQTT_QOS)) {
        LOG_E("MQTT", "No se pudo suscribir a %s: el buffer no se liberara.", _topicoAck.c_str());
    }
    alimentarWatchdog();
    // IOT-004: órdenes de actualización (solo el backend puede publicarlas: ACL).
    if (!_mqtt.subscribe(_topicoOta.c_str(), MQTT_QOS)) {
        LOG_E("MQTT", "No se pudo suscribir a %s: no habra OTA.", _topicoOta.c_str());
    }
    alimentarWatchdog();
    LOG_I("MQTT", "Conectado (%s, QoS %d). Acuses en %s.", certificadoCliente() ? "TLS mutuo" : "TLS",
          MQTT_QOS, _topicoAck.c_str());
    return true;
}

bool MQTTManager::conectado() { return _mqtt.connected(); }

void MQTTManager::loop() {
    _mqtt.loop();
    // HU-09 esc. 2: la sesión abierta sin hora se revisa en cuanto llega SNTP.
    // Un certificado vencido corta el socket (el broker publica el LWT) y el
    // próximo conectar() ya lo rechaza antes del CONNECT.
    if (!_vigenciaPendiente || epochAhora() == 0 || !_mqtt.connected()) return;
    _vigenciaPendiente = false;
    const core::VigenciaCertificado vigencia = _vigenciaBroker();
    if (!core::vigenciaRechaza(vigencia)) return;
    LOG_E("MQTT", "Certificado del broker %s: sesion cerrada (HU-09).", core::nombreVigencia(vigencia));
    _tls.stop();
}

core::VigenciaCertificado MQTTManager::_vigenciaBroker() {
    // mbedTLS (sin CONFIG_MBEDTLS_HAVE_TIME_DATE) ya validó CA y nombre, no las
    // fechas. Se revisa toda la cadena que envió el broker (hoja e intermedias).
    const mbedtls_x509_crt* crt = _tls.getPeerCertificate();
    if (crt == nullptr) return core::VigenciaCertificado::FechaInvalida;  // fallo cerrado
    const uint32_t ahora = epochAhora();
    core::VigenciaCertificado peor = core::VigenciaCertificado::Vigente;
    for (; crt != nullptr; crt = crt->next) {
        if (crt->version == 0) continue;  // nodo vacío de la lista
        const core::FechaX509 desde{crt->valid_from.year, crt->valid_from.mon, crt->valid_from.day,
                                    crt->valid_from.hour, crt->valid_from.min, crt->valid_from.sec};
        const core::FechaX509 hasta{crt->valid_to.year, crt->valid_to.mon, crt->valid_to.day,
                                    crt->valid_to.hour, crt->valid_to.min, crt->valid_to.sec};
        peor = core::peorVigencia(peor, core::evaluarVigencia(desde, hasta, ahora));
    }
    return peor;
}

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
        if (_alEsperarAck) _alEsperarAck();
        alimentarWatchdog();
        delay(10);
    }
}

void MQTTManager::configurarCertificadoCliente(const String& certPem, const String& clavePem) {
    _certPem = certPem;
    _clavePem = clavePem;
    if (_certPem.isEmpty() || _clavePem.isEmpty()) return;
    _tls.setCertificate(_certPem.c_str());
    _tls.setPrivateKey(_clavePem.c_str());
}

const char* MQTTManager::caPem() const {
#if CA_EMBEBIDA
    return ROOT_CA_PEM;
#else
    return _caCargada ? _caPem.c_str() : nullptr;
#endif
}

bool MQTTManager::tomarOrdenOTA(core::OrdenOTA& orden) {
    if (!_hayOrdenOta) return false;
    orden = _ordenOta;
    _hayOrdenOta = false;
    return true;
}

bool MQTTManager::tomarRechazoOTA(std::string& motivo) {
    if (_rechazoOta.empty()) return false;
    motivo = _rechazoOta;
    _rechazoOta.clear();
    return true;
}

bool MQTTManager::tomarDesafioPosesion(std::string& nonce) {
    if (_nonceDesafio.empty()) return false;
    nonce = _nonceDesafio;
    _nonceDesafio.clear();
    return true;
}

void MQTTManager::_alRecibir(String& topico, String& payload) {
    if (topico == _topicoOta) {
        core::OrdenOTA orden;
        std::string motivo;
        if (core::parsearOrdenOTA(std::string(payload.c_str()), std::string(_deviceId.c_str()), orden, motivo)) {
            _ordenOta = orden;
            _hayOrdenOta = true;
        } else {
            _rechazoOta = motivo;
        }
        return;
    }
    if (topico != _topicoAck) return;
    std::string nonce;
    if (core::parsearDesafioPosesion(std::string(payload.c_str()), std::string(_deviceId.c_str()), nonce)) {
        _nonceDesafio = nonce;
        return;
    }
    core::AckLogico ack;
    if (!core::parsearAck(std::string(payload.c_str()), ack) ||
        ack.deviceId != std::string(_deviceId.c_str())) {
        _acksIgnorados++;  // un acuse ambiguo o ajeno nunca libera nada
        return;
    }
    _acks.anotar(ack.bootId, ack.seqNo, ack.estado, ack.contenidoSha256);
}
