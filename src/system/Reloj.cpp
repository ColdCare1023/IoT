#include "Reloj.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <sys/time.h>

#include "../config.h"

static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static core::BaseTiempo _base;
static uint32_t _bootId = 0;
static bool _sntpArrancado = false;
static volatile bool _avisoPendiente = false;
static volatile uint32_t _ultimaEpoch = 0;

uint32_t uptimeSegundos() {
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

// Corre en la tarea de lwIP: nada de Serial ni de bloqueos aquí.
static void alSincronizar(struct timeval* tv) {
    if (tv == nullptr) return;
    const uint32_t epoch = (uint32_t)tv->tv_sec;
    if (epoch < core::EPOCH_MINIMA_VALIDA) return;
    const uint32_t uptime = uptimeSegundos();
    portENTER_CRITICAL(&_mux);
    _base.fijar(_bootId, epoch, uptime);
    portEXIT_CRITICAL(&_mux);
    _ultimaEpoch = epoch;
    _avisoPendiente = true;
}

void iniciarReloj(uint32_t bootId) {
    _bootId = bootId;
    sntp_set_time_sync_notification_cb(alSincronizar);
}

void arrancarSNTP() {
    if (_sntpArrancado) return;
    configTime(0, 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);
    _sntpArrancado = true;
    LOG_I("Reloj", "SNTP en marcha (%s, %s, %s).", NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);
}

core::BaseTiempo baseTiempo() {
    portENTER_CRITICAL(&_mux);
    const core::BaseTiempo copia = _base;
    portEXIT_CRITICAL(&_mux);
    return copia;
}

bool relojSincronizado() {
    return baseTiempo().valida;
}

uint32_t epochAhora() {
    const core::BaseTiempo b = baseTiempo();
    if (!b.valida) return 0;
    const int64_t e = (int64_t)uptimeSegundos() + b.desplazamientoS;
    return (e >= (int64_t)core::EPOCH_MINIMA_VALIDA && e <= 0xFFFFFFFFll) ? (uint32_t)e : 0;
}

bool consumirAvisoSincronizacion(uint32_t& epoch) {
    if (!_avisoPendiente) return false;
    _avisoPendiente = false;
    epoch = _ultimaEpoch;
    return true;
}
