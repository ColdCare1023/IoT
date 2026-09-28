#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <MQTT.h>
#include <WiFiClientSecure.h>

#include <string>

#include "../config.h"

/**
 * Cliente MQTT sobre TLS hacia EMQX Cloud Serverless (HU-09, HU-13).
 *
 *  - Solo `mqtts` en 8883 con `WiFiClientSecure::setCACert()`: el nodo valida
 *    la cadena de confianza y el nombre del servidor (SNI + verificación de
 *    hostname de mbedTLS). No existe ruta a 1883 ni a `setInsecure()`: sin
 *    CA válida el nodo NO conecta y sigue guardando en LittleFS.
 *  - Autenticación: usuario = device_id, contraseña = token propio.
 *  - Publicación QoS 1: `publish()` devuelve true solo con el PUBACK del
 *    broker (lwmqtt verifica el packet id).
 *  - LWT QoS 1 registrado en el CONNECT (HU-13): si el nodo cae sin
 *    despedirse, el broker publica `lwt_offline` al vencer el keep-alive.
 *    Los cierres voluntarios usan DISCONNECT ordenado, que NO dispara el LWT.
 *
 * VERSIÓN DEL PROTOCOLO. La librería (256dpi/arduino-mqtt, lwmqtt) habla
 * MQTT 3.1.1; el core Arduino 2.0.17 (ESP-IDF 4.4) tampoco trae cliente
 * MQTT 5. EMQX Serverless acepta ambos. TLS, QoS 1, LWT, ACL y acuse lógico
 * se cumplen igual; la diferencia visible es el código de rechazo: 3.1.1
 * devuelve 0x05 "Not authorized" donde MQTT 5 devolvería 0x87.
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

    Publicacion publicar(const String& topico, const std::string& payload);

    const String& topicoLecturas() const { return _topicoLecturas; }
    const String& topicoEventos() const { return _topicoEventos; }

    bool caCargada() const { return _caCargada; }
    bool aprovisionado() const { return _aprovisionado; }
    int ultimoCodigoRetorno() { return (int)_mqtt.returnCode(); }
    int ultimoError() { return (int)_mqtt.lastError(); }

private:
    bool _cargarCA();

    WiFiClientSecure& _tls;
    MQTTClient _mqtt{MQTT_BUFFER_BYTES, MQTT_BUFFER_BYTES};

    String _host;
    String _deviceId;
    String _token;
    String _caPem;  // setCACert guarda el PUNTERO: la cadena debe vivir siempre
    String _topicoLecturas;
    String _topicoEventos;
    std::string _lwt;

    bool _caCargada = false;
    bool _aprovisionado = false;
    uint32_t _ultimoIntentoMs = 0;
};

#endif  // MQTT_MANAGER_H
