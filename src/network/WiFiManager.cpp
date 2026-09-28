#include "WiFiManager.h"

#include <WiFi.h>

#include "../core/Credenciales.h"
#include "../system/Watchdog.h"

void WiFiManager::configurar(const String& ssid, const String& password) {
    _ssid = ssid;
    _password = password;
}

bool WiFiManager::tieneCredenciales() const {
    return core::credencialValida(std::string(_ssid.c_str()));
}

void WiFiManager::begin() {
    if (!tieneCredenciales()) {
        LOG_E("WiFi", "Sin SSID aprovisionado: el nodo captura y guarda solo en modo offline.");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // la política de reintentos es nuestra (backoff)
    WiFi.setSleep(false);          // latencia estable para el acuse lógico
    _ultimoIntentoMs = millis();
    _conectar();
}

bool WiFiManager::conectado() const {
    return WiFi.status() == WL_CONNECTED;
}

int WiFiManager::rssi() const {
    return conectado() ? WiFi.RSSI() : 0;
}

bool WiFiManager::mantener() {
    if (conectado()) return true;
    if (!tieneCredenciales()) return false;
    const uint32_t ahora = millis();
    if (!_backoff.debeIntentar(ahora, _ultimoIntentoMs)) return false;
    _ultimoIntentoMs = ahora;
    LOG_I("WiFi", "Reintento #%lu (backoff %lu ms).", (unsigned long)(_backoff.intentos() + 1),
          (unsigned long)_backoff.retardoMs());
    return _conectar();
}

bool WiFiManager::_conectar() {
    WiFi.disconnect(false, false);
    WiFi.begin(_ssid.c_str(), _password.c_str());

    const uint32_t inicio = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - inicio > WIFI_TIMEOUT_ASOCIACION_MS) {
            _backoff.registrarFallo();
            LOG_E("WiFi", "Sin asociacion con '%s' (intento %lu, proximo en %lu ms).",
                  _ssid.c_str(), (unsigned long)_backoff.intentos(),
                  (unsigned long)_backoff.retardoMs());
            if (!_diagnosticoActivo && _backoff.intentos() >= WIFI_UMBRAL_DIAGNOSTICO_INTENTOS) {
                _diagnosticoActivo = true;
                _inicioDiagnosticoMs = millis();
                LOG_E("WiFi", "DIAGNOSTICO: %lu fallos seguidos. AP ausente o credenciales "
                              "invalidas. La captura offline continua.",
                      (unsigned long)_backoff.intentos());
            }
            return false;
        }
        alimentarWatchdog();
        delay(500);
    }

    if (_diagnosticoActivo) {
        _recuperacion.intentos = _backoff.intentos();
        _recuperacion.duracionS = (millis() - _inicioDiagnosticoMs) / 1000;
        _diagnosticoActivo = false;
        _recuperacionPendiente = true;
    }
    _backoff.registrarExito();
    LOG_I("WiFi", "Conectado: IP %s, RSSI %d dBm.", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

bool WiFiManager::consumirRecuperacion(Recuperacion& r) {
    if (!_recuperacionPendiente) return false;
    _recuperacionPendiente = false;
    r = _recuperacion;
    return true;
}
