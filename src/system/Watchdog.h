#ifndef SYSTEM_WATCHDOG_H
#define SYSTEM_WATCHDOG_H

#include "../config.h"

/**
 * Task Watchdog (TWDT) sobre las dos tareas que importan: captura y red.
 *
 * Plazo = 2 × el mayor tramo que NO puede alimentar el watchdog. Ese tramo es
 * `mqtt.connect()`: handshake TLS + espera del CONNACK dentro de una sola
 * llamada del SDK (10 s + 5 s). Todo lo demás alimenta el watchdog en su
 * bucle: asociación Wi-Fi (cada 500 ms), conversión DS18B20 (cada 10 ms),
 * espera del acuse lógico (cada 10 ms) y la ventana de muestreo (cada 50 ms).
 * SNTP es asíncrono (callback), así que ya no bloquea.
 *
 * Los reinicios por watchdog se registran: el motivo de reinicio viaja en el
 * evento `arranque` y en la línea [BOOT] del monitor serie (ensayo doméstico).
 */

static const unsigned int PEOR_BLOQUEO_NO_ALIMENTABLE_MS = TLS_HANDSHAKE_TIMEOUT_MS + MQTT_COMMAND_TIMEOUT_MS;
static const unsigned int WATCHDOG_TIMEOUT_S = (2 * PEOR_BLOQUEO_NO_ALIMENTABLE_MS + 999) / 1000;

// Si el watchdog superara el keep-alive, EMQX publicaría el LWT antes de que
// el nodo llegara a reiniciarse: aparecería "caído" sin recuperarse.
static_assert(WATCHDOG_TIMEOUT_S <= MQTT_KEEPALIVE_S,
              "El plazo del watchdog supera el keep-alive MQTT: revisar timeouts en config.h");
static_assert(ACK_LOGICO_TIMEOUT_MS < WATCHDOG_TIMEOUT_S * 1000u,
              "La espera del acuse logico debe caber en el plazo del watchdog");

void inicializarWatchdog();
void suscribirTareaAlWatchdog(const char* nombreTarea);
/// Seguro de llamar desde tareas no suscritas (no hace nada).
void alimentarWatchdog();

#endif  // SYSTEM_WATCHDOG_H
