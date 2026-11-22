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
 * Diagnóstico (HU-08 esc. 3): cada asociación fallida suma en
 * `fallosTotales()`. El episodio (intentos, desde cuándo, reinicios en medio)
 * lo lleva `core::SeguimientoReconexion`, persistido en NVS para que un
 * reinicio por watchdog no lo borre (system/DiagnosticoReconexion.h); se
 * reporta al volver la sesión MQTT y en cada `estado_nodo`.
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
    /// Asociaciones fallidas desde el arranque (no se reinicia al conectar).
    uint32_t fallosTotales() const { return _fallosTotales; }

private:
    bool _conectar();
    String _ssid;
    String _password;
    core::Backoff _backoff{WIFI_BACKOFF_BASE_MS, WIFI_BACKOFF_MAX_MS, WIFI_BACKOFF_FACTOR};
    uint32_t _ultimoIntentoMs = 0;
    uint32_t _fallosTotales = 0;
};

#endif  // WIFI_MANAGER_H
