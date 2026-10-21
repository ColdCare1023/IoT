#ifndef SYSTEM_RELOJ_H
#define SYSTEM_RELOJ_H

#include <cstdint>

#include "../core/Tiempo.h"

/**
 * Hora UTC por SNTP + uptime monótono (ver core/Tiempo.h).
 *
 * SNTP es asíncrono: `arrancarSNTP()` solo lo pone en marcha y el callback de
 * sincronización fija la base época↔uptime de ESTE arranque. Nada bloquea
 * esperando la hora, y sin hora no se inventa ninguna: captured_at = null.
 */

/// Registra el callback de SNTP. Llamar en setup(), con el boot_id ya fijado.
void iniciarReloj(uint32_t bootId);

/// Arranca SNTP (idempotente). Requiere la pila de red ya inicializada, así
/// que se llama la primera vez que hay Wi-Fi.
void arrancarSNTP();

uint32_t uptimeSegundos();
bool relojSincronizado();
core::BaseTiempo baseTiempo();

/// Época actual si SNTP sincronizó en este arranque; 0 si no.
uint32_t epochAhora();

/// true una vez tras cada sincronización (para registrarla en el log).
bool consumirAvisoSincronizacion(uint32_t& epoch);

/// IOT-007: sincronizaciones SNTP rechazadas por saltar respecto de la base
/// vigente del arranque (ver core::evaluarSincronizacion).
uint32_t saltosRelojRechazados();
bool consumirAvisoSaltoRechazado();

#endif  // SYSTEM_RELOJ_H
