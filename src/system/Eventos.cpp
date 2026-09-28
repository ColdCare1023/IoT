#include "Eventos.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <deque>

#include "../config.h"
#include "Reloj.h"

static SemaphoreHandle_t _mutex = nullptr;
static std::deque<EventoPendiente> _cola;
static uint32_t _descartados = 0;

void iniciarEventos() {
    if (_mutex == nullptr) _mutex = xSemaphoreCreateMutex();
}

void encolarEvento(const char* tipo, const std::string& detalleJSON) {
    EventoPendiente e;
    e.tipo = tipo;
    e.detalleJSON = detalleJSON;
    e.epoch = epochAhora();
    e.uptimeS = uptimeSegundos();
    if (_mutex == nullptr) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_cola.size() >= CAPACIDAD_EVENTOS_RAM) {
        _cola.pop_front();
        _descartados++;
    }
    _cola.push_back(e);
    xSemaphoreGive(_mutex);
    LOG_I("Evento", "Encolado '%s'.", tipo);
}

bool tomarEvento(EventoPendiente& evento) {
    if (_mutex == nullptr) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool hay = !_cola.empty();
    if (hay) {
        evento = _cola.front();
        _cola.pop_front();
    }
    xSemaphoreGive(_mutex);
    return hay;
}

void devolverEvento(const EventoPendiente& evento) {
    if (_mutex == nullptr) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_cola.size() < CAPACIDAD_EVENTOS_RAM) _cola.push_front(evento);
    else _descartados++;
    xSemaphoreGive(_mutex);
}

uint32_t eventosPendientes() {
    if (_mutex == nullptr) return 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const uint32_t n = (uint32_t)_cola.size();
    xSemaphoreGive(_mutex);
    return n;
}

uint32_t eventosDescartados() { return _descartados; }
