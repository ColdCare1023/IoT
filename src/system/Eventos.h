#ifndef SYSTEM_EVENTOS_H
#define SYSTEM_EVENTOS_H

#include <Arduino.h>

#include <string>

/**
 * Cola en RAM de eventos de diagnóstico pendientes de publicar en
 * `farmacias/<device_id>/eventos` (arranque, buffer al 80 %, saturación,
 * conmutación a respaldo, reconexión prolongada...).
 *
 * Se llena desde cualquier tarea y la vacía la tarea de red cuando hay
 * sesión MQTT. Los eventos son diagnóstico, NO la variable del experimento:
 * lo que no puede perderse (lecturas, contador de pérdidas) está en LittleFS
 * y NVS. Si la cola se llena se descarta el más antiguo y se cuenta.
 */

struct EventoPendiente {
    String tipo;
    std::string detalleJSON;  ///< objeto JSON o "" para null
    uint32_t epoch = 0;       ///< 0 si no había SNTP al ocurrir
    uint32_t uptimeS = 0;
};

void iniciarEventos();

/// Encola con la hora actual (epoch si hay SNTP, siempre uptime).
void encolarEvento(const char* tipo, const std::string& detalleJSON);

/// Saca el más antiguo. false si no hay.
bool tomarEvento(EventoPendiente& evento);

/// Devuelve un evento no publicado al frente de la cola.
void devolverEvento(const EventoPendiente& evento);

uint32_t eventosPendientes();
uint32_t eventosDescartados();

#endif  // SYSTEM_EVENTOS_H
