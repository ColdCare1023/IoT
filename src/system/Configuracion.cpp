#include "Configuracion.h"

#include <Preferences.h>

#include "../config.h"
#include "../core/Consola.h"
#include "../core/Credenciales.h"
#include "../core/Identidad.h"
#include "../core/Payload.h"

static const char* const CLAVES_TEXTO[] = {"wifi_ssid", "wifi_pass", "mqtt_host", "mqtt_token",
                                           "mqtt_relevo", "device_id"};

bool ConfiguracionNodo::credencialesCompletas() const {
    return core::aprovisionamientoCompleto(std::string(wifiSsid.c_str()),
                                           std::string(mqttHost.c_str()),
                                           std::string(mqttToken.c_str()));
}

static String elegir(Preferences& nvs, bool hayNvs, const char* clave, const char* respaldo,
                     bool& vinoDeNvs) {
    vinoDeNvs = false;
    if (hayNvs) {
        const String valor = nvs.getString(clave, "");
        if (core::credencialValida(std::string(valor.c_str()))) {
            vinoDeNvs = true;
            return valor;
        }
    }
    return String(respaldo != nullptr ? respaldo : "");
}

static uint32_t acotarIntervalo(uint32_t s) {
    if (s < INTERVALO_MUESTREO_S_MIN) return INTERVALO_MUESTREO_S_MIN;
    if (s > INTERVALO_MUESTREO_S_MAX) return INTERVALO_MUESTREO_S_MAX;
    return s;
}

ConfiguracionNodo cargarConfiguracion() {
    ConfiguracionNodo c;
    Preferences nvs;
    // Solo lectura: si el namespace no existe, begin() falla y se usan las
    // build_flags. No es un error: es un nodo aún no aprovisionado por NVS.
    const bool hayNvs = nvs.begin(NVS_NAMESPACE, true);

    bool a = false, b = false, h = false, t = false;
    c.wifiSsid = elegir(nvs, hayNvs, "wifi_ssid", WIFI_SSID, a);
    c.wifiPassword = elegir(nvs, hayNvs, "wifi_pass", WIFI_PASSWORD, b);
    c.mqttHost = elegir(nvs, hayNvs, "mqtt_host", MQTT_HOST, h);
    c.mqttToken = elegir(nvs, hayNvs, "mqtt_token", MQTT_TOKEN, t);

    c.intervaloMuestreoS = INTERVALO_MUESTREO_S_DEFECTO;
    String idNvs;
    if (hayNvs) {
        idNvs = nvs.getString("device_id", "");
        c.intervaloMuestreoS = acotarIntervalo(nvs.getUInt("intervalo_s", INTERVALO_MUESTREO_S_DEFECTO));
        nvs.end();
    }
    // IOT-005: el firmware demo nunca hereda la identidad productiva de NVS.
    const core::Identidad identidad =
        core::resolverIdentidad(DEVICE_ID, std::string(idNvs.c_str()), MODO_DEMO != 0);
    c.deviceId = identidad.deviceId.c_str();
    c.identidadBloqueada = identidad.bloqueada;

    if (a && h && t)        c.origen = "NVS";
    else if (!a && !h && !t) c.origen = "build_flags";
    else                     c.origen = "mixto (NVS + build_flags)";
    return c;
}

String releerTokenMqtt() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return String(MQTT_TOKEN);
    String token = nvs.getString("mqtt_token", "");
    nvs.end();
    if (!core::credencialValida(std::string(token.c_str()))) token = MQTT_TOKEN;
    return token;
}

String leerTokenRelevo() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return String();
    const String relevo = nvs.getString("mqtt_relevo", "");
    nvs.end();
    return core::credencialValida(std::string(relevo.c_str())) ? relevo : String();
}

bool promoverTokenRelevo(const String& relevo) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = nvs.putString("mqtt_token", relevo) == relevo.length();
    if (ok) nvs.remove("mqtt_relevo");
    nvs.end();
    return ok;
}

void descartarTokenRelevo() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return;
    nvs.remove("mqtt_relevo");
    nvs.end();
}

uint32_t cargarEIncrementarBootId() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return 0;
    const uint32_t nuevo = nvs.getUInt("boot_id", 0) + 1;
    nvs.putUInt("boot_id", nuevo);
    nvs.end();
    return nuevo;
}

static bool esClaveTexto(const String& clave) {
    for (const char* k : CLAVES_TEXTO) {
        if (clave == k) return true;
    }
    return false;
}

static const char* const CLAVE_HUELLA_SERVICIO = "svc_hash";

String huellaClaveServicio() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return String();
    const String huella = nvs.getString(CLAVE_HUELLA_SERVICIO, "");
    nvs.end();
    return huella.length() == 64 ? huella : String();
}

bool guardarClaveNvs(const String& clave, const String& valor) {
    if (clave == "clave_servicio") {
        // IOT-002: nunca se guarda la clave, solo su huella.
        if (!core::claveServicioAceptable(std::string(valor.c_str()))) return false;
        const std::string huella = core::huellaClaveServicio(std::string(valor.c_str()));
        Preferences nvs;
        if (!nvs.begin(NVS_NAMESPACE, false)) return false;
        const bool ok = nvs.putString(CLAVE_HUELLA_SERVICIO, huella.c_str()) == huella.size();
        nvs.end();
        return ok;
    }
    if (clave == "intervalo_s") {
        const long s = valor.toInt();
        if (s < INTERVALO_MUESTREO_S_MIN || s > INTERVALO_MUESTREO_S_MAX) return false;
        Preferences nvs;
        if (!nvs.begin(NVS_NAMESPACE, false)) return false;
        const bool ok = nvs.putUInt("intervalo_s", (uint32_t)s) > 0;
        nvs.end();
        return ok;
    }
    if (!esClaveTexto(clave)) return false;
    // HU-14: el host no puede traer esquema ni puerto (8883/TLS es fijo).
    if (clave == "mqtt_host" && !core::hostMqttValido(std::string(valor.c_str()))) return false;
    if (clave == "device_id" && !core::deviceIdValido(std::string(valor.c_str()))) return false;

    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = nvs.putString(clave.c_str(), valor) == valor.length();
    nvs.end();
    return ok;
}

bool borrarClaveNvs(const String& clave) {
    const bool esServicio = clave == "clave_servicio";
    if (!esClaveTexto(clave) && clave != "intervalo_s" && !esServicio) return false;
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = nvs.remove(esServicio ? CLAVE_HUELLA_SERVICIO : clave.c_str());
    nvs.end();
    return ok;
}
