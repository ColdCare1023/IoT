#include "CertificadoCliente.h"

#include <Preferences.h>
#include <WiFi.h>

#include "../config.h"
#include "../core/Payload.h"
#include "../core/Pem.h"
#include "../tls/IdentidadTLS.h"

static const char* const CLAVE_NVS_KEY = "mqtt_key";
static const char* const CLAVE_NVS_CERT = "mqtt_cert";

static String leer(const char* clave) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return String();
    const String valor = nvs.getString(clave, "");
    nvs.end();
    return valor;
}

bool cargarCertificadoCliente(const String& deviceId, String& certPem, String& clavePem) {
    const String cert = leer(CLAVE_NVS_CERT);
    const String clave = leer(CLAVE_NVS_KEY);
    if (cert.isEmpty() || clave.isEmpty()) return false;
    // Un device_id cambiado o datos dañados dejarían al nodo reintentando un
    // handshake que el broker rechaza: se comprueba antes de usarlos.
    const tls::ResultadoCertificado r =
        tls::validarCertificado(std::string(cert.c_str()), std::string(clave.c_str()),
                                std::string(deviceId.c_str()));
    if (r != tls::ResultadoCertificado::Valido) {
        LOG_E("TLS", "Certificado de cliente guardado no utilizable (%s): se conecta sin el.",
              tls::nombreResultado(r));
        return false;
    }
    certPem = cert;
    clavePem = clave;
    return true;
}

ResultadoCsr prepararCsr(const String& deviceId, bool nuevaClave, String& csrPem, bool& claveNueva) {
    claveNueva = false;
    if (!core::deviceIdValido(std::string(deviceId.c_str()))) return ResultadoCsr::SinDeviceId;
    String clave = nuevaClave ? String() : leer(CLAVE_NVS_KEY);
    if (clave.isEmpty()) {
        // El RNG del ESP32 solo es aleatorio de verdad con la radio encendida.
        if (WiFi.getMode() == WIFI_MODE_NULL) return ResultadoCsr::SinRadio;
        std::string generada;
        if (!tls::generarClavePem(generada)) return ResultadoCsr::Fallo;
        Preferences nvs;
        if (!nvs.begin(NVS_NAMESPACE, false)) return ResultadoCsr::Fallo;
        nvs.remove(CLAVE_NVS_CERT);  // el certificado anterior era de otra clave
        const bool ok = nvs.putString(CLAVE_NVS_KEY, generada.c_str()) == generada.size();
        nvs.end();
        if (!ok) return ResultadoCsr::Fallo;
        clave = String(generada.c_str());
        claveNueva = true;
    }
    std::string csr;
    if (!tls::generarCsrPem(std::string(clave.c_str()), std::string(deviceId.c_str()), csr)) {
        return ResultadoCsr::Fallo;
    }
    csrPem = String(csr.c_str());
    return ResultadoCsr::Generada;
}

bool instalarCertificadoCliente(const String& deviceId, const String& base64Der, String& motivo) {
    const std::string pem = core::pemDesdeBase64("CERTIFICATE", std::string(base64Der.c_str()));
    if (pem.empty()) {
        motivo = "base64_invalido";
        return false;
    }
    const String clave = leer(CLAVE_NVS_KEY);
    if (clave.isEmpty()) {
        motivo = "sin_clave";
        return false;
    }
    const tls::ResultadoCertificado r =
        tls::validarCertificado(pem, std::string(clave.c_str()), std::string(deviceId.c_str()));
    if (r != tls::ResultadoCertificado::Valido) {
        motivo = tls::nombreResultado(r);
        return false;
    }
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) {
        motivo = "nvs";
        return false;
    }
    const bool ok = nvs.putString(CLAVE_NVS_CERT, pem.c_str()) == pem.size();
    nvs.end();
    motivo = ok ? "" : "nvs";
    return ok;
}

bool borrarCertificadoCliente() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    nvs.remove(CLAVE_NVS_CERT);
    nvs.remove(CLAVE_NVS_KEY);
    nvs.end();
    return true;
}
