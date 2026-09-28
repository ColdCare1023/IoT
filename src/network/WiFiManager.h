#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

#include "../config.h"
#include "../core/Backoff.h"

/**
 * Wi-Fi con backoff exponencial (HU-08): 1 s → 2 s → 4 s … → 60 s de tope;
 * al reconectar el retardo vuelve a 1 s.
 *
 * Corre en la tarea de red: una asociación que tarda 15 s NO frena la
 * captura, que vive en la otra tarea y sigue guardando en LittleFS.
 *
 * Diagnóstico (HU-08 esc. 3): tras WIFI_UMBRAL_DIAGNOSTICO_INTENTOS fallos
 * seguidos se marca la señal de diagnóstico (credenciales inválidas o AP
 * ausente). Solo puede publicarse al volver la red: se reporta entonces con
 * cuántos intentos y cuánto duró el episodio.
 */
class WiFiManager {
public:
    void configurar(const String& ssid, const String& password);
    void begin();
    bool tieneCredenciales() const;
    bool conectado() const;

    /// Reconecta si toca según el backoff. Devuelve true si hay conexión.
    bool mantener();

    int rssi() const;
    uint32_t intentosFallidos() const { return _backoff.intentos(); }
    uint32_t retardoActualMs() const { return _backoff.retardoMs(); }
    bool diagnosticoActivo() const { return _diagnosticoActivo; }

    struct Recuperacion {
        uint32_t intentos = 0;
        uint32_t duracionS = 0;
    };
    /// true una sola vez, al reconectar después de un episodio de diagnóstico.
    bool consumirRecuperacion(Recuperacion& r);

private:
    bool _conectar();
    String _ssid;
    String _password;
    core::Backoff _backoff{WIFI_BACKOFF_BASE_MS, WIFI_BACKOFF_MAX_MS, WIFI_BACKOFF_FACTOR};
    uint32_t _ultimoIntentoMs = 0;
    bool _diagnosticoActivo = false;
    uint32_t _inicioDiagnosticoMs = 0;
    bool _recuperacionPendiente = false;
    Recuperacion _recuperacion;
};

#endif  // WIFI_MANAGER_H
