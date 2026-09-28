#include "Credenciales.h"

#include <cctype>

namespace core {

bool esMarcadorDeDesarrollo(const std::string& valor) {
    static const char* marcadores[] = {
        "cambiar_en_produccion",
        "token_generado_en_emqx",
        "tu-instancia.emqx.cloud",
        "CAMBIAR",
        "changeme",
        "placeholder-ci",
    };
    for (const char* m : marcadores) {
        if (valor == m) return true;
    }
    return false;
}

bool credencialValida(const std::string& valor) {
    if (valor.empty()) return false;
    bool soloEspacios = true;
    for (char c : valor) {
        if (!std::isspace((unsigned char)c)) { soloEspacios = false; break; }
    }
    if (soloEspacios) return false;
    return !esMarcadorDeDesarrollo(valor);
}

bool hostMqttValido(const std::string& host) {
    if (!credencialValida(host) || host.size() > 253) return false;
    for (char c : host) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;  // ':', '/', espacios, esquemas...
    }
    return host.front() != '.' && host.front() != '-' && host.back() != '.';
}

bool aprovisionamientoCompleto(const std::string& ssid,
                               const std::string& mqttHost,
                               const std::string& mqttToken) {
    // La contraseña del Wi-Fi NO entra en la comprobación: una red abierta es
    // una configuración legítima de laboratorio. El token del broker sí, porque
    // EMQX rechaza la conexión sin él.
    return credencialValida(ssid) && hostMqttValido(mqttHost) &&
           credencialValida(mqttToken);
}

}  // namespace core
