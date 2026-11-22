#ifndef SYSTEM_WATCHDOG_H
#define SYSTEM_WATCHDOG_H

#include "../config.h"
#include "../core/PlazosConexion.h"

/**
 * Task Watchdog (TWDT) sobre las dos tareas que importan: captura y red.
 *
 * Plazo = el mayor tramo que NO puede alimentar el watchdog + margen, sin
 * pasar del keep-alive MQTT. Ese tramo es `mqtt.connect()`: DNS, conexión TCP,
 * handshake TLS y CONNACK dentro de una sola llamada del SDK (21 s + 10 s +
 * 20 s + 5 s; el cálculo y sus fuentes están en core/PlazosConexion.h). Con el
 * host del broker inalcanzable, un plazo menor reiniciaba el nodo en bucle (H1
 * del banco). Todo lo demás alimenta el watchdog: asociación Wi-Fi (cada
 * 500 ms), cada SUBACK de `MQTTManager::conectar()`, conversión DS18B20 (cada
 * 10 ms), espera del acuse lógico (cada 10 ms) y la ventana de muestreo (cada
 * 50 ms). SNTP es asíncrono (callback), así que ya no bloquea.
 *
 * Los reinicios por watchdog se registran: el motivo de reinicio viaja en el
 * evento `arranque` y en la línea [BOOT] del monitor serie (ensayo doméstico),
 * y el episodio de reconexión en curso sobrevive al reinicio (NVS, HU-08).
 */

static const unsigned int PEOR_BLOQUEO_NO_ALIMENTABLE_MS = core::PEOR_BLOQUEO_CONEXION_MS;
static const unsigned int WATCHDOG_TIMEOUT_S = core::WATCHDOG_PLAZO_S;

void inicializarWatchdog();
void suscribirTareaAlWatchdog(const char* nombreTarea);
/// Seguro de llamar desde tareas no suscritas (no hace nada).
void alimentarWatchdog();

#endif  // SYSTEM_WATCHDOG_H
