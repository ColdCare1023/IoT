#ifndef CORE_PLAZOS_CONEXION_H
#define CORE_PLAZOS_CONEXION_H

#include <cstdint>

#include "../config.h"

namespace core {

/**
 * Plazos de la conexión al broker, con la UNIDAD en el nombre (H1 del banco;
 * HU-08 esc. 3, HU-09 esc. 3).
 *
 * El defecto H1 fue un error de unidades: `Stream::setTimeout()` recibía
 * `TLS_HANDSHAKE_TIMEOUT_MS / 1000` (10), así que el `connect()` TCP del core
 * tenía 10 ms de plazo, y el socket, 10 ms para cada lectura y escritura. Con el
 * broker fuera de la red local cada intento fallaba al instante, y el watchdog
 * no cubría el resto del intento (DNS, TLS, CONNACK): el nodo entraba en un
 * bucle de reinicios por TASK_WDT.
 *
 * Firmas verificadas en el paquete fijado en platformio.ini
 * (framework-arduinoespressif32 3.3.12, pioarduino 55.03.312-1):
 *   - `Stream::setTimeout(unsigned long timeout)`: MILISEGUNDOS.
 *     `NetworkClientSecure::connect()` lo pasa a `start_ssl_client()` como plazo
 *     del `connect()` TCP (select) y como SO_RCVTIMEO/SO_SNDTIMEO del socket.
 *   - `NetworkClientSecure::setHandshakeTimeout(unsigned long)`: SEGUNDOS
 *     (guarda `handshake_timeout * 1000`).
 *   - `MQTTClient::setTimeout(int)` (256dpi/MQTT 2.5.3): MILISEGUNDOS
 *     (espera del CONNACK, del PUBACK y del SUBACK).
 *
 * `MQTTManager::configurar()` pasa estas constantes y nada más. La prueba
 * nativa `test_plazos_conexion` revisa ese código fuente para que no vuelva a
 * aparecer una división por 1000.
 */
static constexpr uint32_t PLAZO_CLIENTE_TLS_MS = TLS_HANDSHAKE_TIMEOUT_MS;
static constexpr uint32_t PLAZO_HANDSHAKE_TLS_S = TLS_HANDSHAKE_TIMEOUT_MS / 1000u;
static constexpr uint32_t PLAZO_COMANDO_MQTT_MS = MQTT_COMMAND_TIMEOUT_MS;

// Un plazo en milisegundos menor que 1 s es, casi seguro, un valor en segundos.
static_assert(PLAZO_CLIENTE_TLS_MS >= 1000u,
              "H1: Stream::setTimeout va en milisegundos; un plazo < 1 s es un valor en segundos");
static_assert(PLAZO_COMANDO_MQTT_MS >= 1000u, "MQTTClient::setTimeout va en milisegundos");
// Y uno en segundos mayor que un minuto es un valor en milisegundos.
static_assert(PLAZO_HANDSHAKE_TLS_S >= 1u && PLAZO_HANDSHAKE_TLS_S <= 60u,
              "setHandshakeTimeout va en segundos (1..60)");
static_assert(PLAZO_HANDSHAKE_TLS_S * 1000u == TLS_HANDSHAKE_TIMEOUT_MS,
              "TLS_HANDSHAKE_TIMEOUT_MS debe ser un numero entero de segundos");

/// lwIP del core (opt.h y sdkconfig del paquete): reintentos por servidor DNS
/// y servidores DNS que admite la pila.
static constexpr uint32_t DNS_REINTENTOS_LWIP = 4;   // DNS_MAX_RETRIES
static constexpr uint32_t DNS_SERVIDORES_LWIP = 3;   // CONFIG_LWIP_DNS_MAX_SERVERS

/// Espera de lwIP por cada servidor DNS que no responde. El primer envío espera
/// 1 s y cada reintento `k` espera `k` s (dns.c: `tmr = retries`): con 4
/// reintentos, 1 + 1 + 2 + 3 = 7 s.
constexpr uint32_t esperaDnsPorServidorMs(uint32_t reintentos) {
    return reintentos == 0 ? 0u : 1000u + 1000u * ((reintentos - 1u) * reintentos / 2u);
}

static constexpr uint32_t DNS_PEOR_BLOQUEO_DERIVADO_MS =
    esperaDnsPorServidorMs(DNS_REINTENTOS_LWIP) * DNS_SERVIDORES_LWIP;

static_assert(DNS_PEOR_BLOQUEO_MS >= DNS_PEOR_BLOQUEO_DERIVADO_MS,
              "DNS_PEOR_BLOQUEO_MS no cubre la espera de lwIP con todos sus servidores DNS");

/**
 * Peor bloqueo de `MQTTClient::connect()`, una sola llamada de la librería que
 * no puede alimentar el watchdog:
 *   - DNS (`lwip_getaddrinfo`): `DNS_PEOR_BLOQUEO_MS`.
 *   - TCP: `PLAZO_CLIENTE_TLS_MS`.
 *   - TLS: el bucle del handshake revisa su plazo ENTRE llamadas a mbedTLS, y
 *     cada llamada puede quedar bloqueada una lectura entera (SO_RCVTIMEO =
 *     `PLAZO_CLIENTE_TLS_MS`): handshake + una lectura.
 *   - CONNACK: `PLAZO_COMANDO_MQTT_MS`.
 */
static constexpr uint32_t PEOR_BLOQUEO_CONEXION_MS = DNS_PEOR_BLOQUEO_MS + PLAZO_CLIENTE_TLS_MS +
                                                     TLS_HANDSHAKE_TIMEOUT_MS + PLAZO_CLIENTE_TLS_MS +
                                                     PLAZO_COMANDO_MQTT_MS;

/// Margen del watchdog sobre ese bloqueo. Lo limita el keep-alive (abajo).
static constexpr uint32_t MARGEN_WATCHDOG_MS = 4000;
static constexpr uint32_t WATCHDOG_PLAZO_S = (PEOR_BLOQUEO_CONEXION_MS + MARGEN_WATCHDOG_MS + 999u) / 1000u;

static_assert(WATCHDOG_PLAZO_S * 1000u > PEOR_BLOQUEO_CONEXION_MS,
              "El watchdog debe durar mas que el peor intento de conexion al broker");
// Si el watchdog superara el keep-alive, EMQX publicaría el LWT antes de que
// el nodo llegara a reiniciarse: aparecería "caído" sin recuperarse.
static_assert(WATCHDOG_PLAZO_S <= MQTT_KEEPALIVE_S,
              "El plazo del watchdog supera el keep-alive MQTT: revisar timeouts en config.h");
static_assert(ACK_LOGICO_TIMEOUT_MS < WATCHDOG_PLAZO_S * 1000u,
              "La espera del acuse logico debe caber en el plazo del watchdog");

}  // namespace core

#endif  // CORE_PLAZOS_CONEXION_H
