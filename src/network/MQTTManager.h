#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <MQTT.h>
#include <WiFiClientSecure.h>

#include <string>

#include "../config.h"
#include "../core/Ack.h"

/**
 * Cliente MQTT sobre TLS hacia EMQX Cloud Serverless (HU-09, HU-11, HU-13, HU-14).
 *
 *  - Solo `mqtts` en 8883 con `WiFiClientSecure::setCACert()`: el nodo valida
 *    la cadena de confianza y el nombre del servidor (SNI + verificación de
 *    hostname de mbedTLS). No existe ruta a 1883 ni a `setInsecure()`: sin
 *    CA válida el nodo NO conecta y sigue guardando en LittleFS.
 *  - Autenticación: usuario = device_id, contraseña = token propio (HU-10).
 *  - Publicación QoS 1: `publish()` devuelve true solo con el PUBACK del
 *    broker (lwmqtt verifica el packet id).
 *  - LWT QoS 1 registrado en el CONNECT (HU-13); DISCONNECT ordenado para
 *    cierres voluntarios, que NO disparan el LWT.
 *  - Suscripción a `.../ack`: los acuses lógicos se guardan en `MemoriaAcks`.
 *
 * VERSIÓN DEL PROTOCOLO. La librería (256dpi/arduino-mqtt, lwmqtt) habla
 * MQTT 3.1.1; el core Arduino 2.0.17 (ESP-IDF 4.4) tampoco trae cliente
 * MQTT 5. EMQX Serverless acepta ambos. Lo que exige el backlog —TLS, QoS 1,
 * LWT, ACL por tópico, acuse lógico— se cumple igual; la diferencia visible es
 * el código de rechazo: 3.1.1 devuelve 0x05 "Not authorized" donde MQTT 5
 * devolvería 0x87. Ver docs/decisiones_tecnicas.md.
 */
class MQTTManager {
public:
    enum class Publicacion { Confirmada, SinPuback, SinConexion };

    explicit MQTTManager(WiFiClientSecure& tls);

    /// Carga la CA desde LittleFS y prepara el cliente. Llamar una vez.
    void configurar(const String& host, const String& deviceId, const String& token);

    bool conectar();
    bool conectado();
    void loop();
    void desconectarOrdenadamente();

    /// HU-44: el nuevo token se usa en el PRÓXIMO conectar().
    void actualizarToken(const String& token);
    const String& token() const { return _token; }

    Publicacion publicar(const String& topico, const std::string& payload);

    /// Espera el acuse lógico de (boot_id, seq_no) bombeando loop().
    bool esperarAck(uint32_t bootId, uint32_t seqNo, uint32_t timeoutMs, core::EstadoAck& estado);
    core::MemoriaAcks& acks() { return _acks; }

    const String& topicoLecturas() const { return _topicoLecturas; }
    const String& topicoEventos() const { return _topicoEventos; }
    const String& topicoAck() const { return _topicoAck; }

    bool caCargada() const { return _caCargada; }
    bool aprovisionado() const { return _aprovisionado; }
    int ultimoCodigoRetorno() { return (int)_mqtt.returnCode(); }
    int ultimoError() { return (int)_mqtt.lastError(); }
    uint32_t acksIgnorados() const { return _acksIgnorados; }

private:
    bool _cargarCA();
    void _alRecibir(String& topico, String& payload);

    WiFiClientSecure& _tls;
    MQTTClient _mqtt{MQTT_BUFFER_BYTES, MQTT_BUFFER_BYTES};
    core::MemoriaAcks _acks;

    String _host;
    String _deviceId;
    String _token;
    String _caPem;  // setCACert guarda el PUNTERO: la cadena debe vivir siempre
    String _topicoLecturas;
    String _topicoEventos;
    String _topicoAck;
    std::string _lwt;

    bool _caCargada = false;
    bool _aprovisionado = false;
    uint32_t _ultimoIntentoMs = 0;
    uint32_t _acksIgnorados = 0;
};

#endif  // MQTT_MANAGER_H
