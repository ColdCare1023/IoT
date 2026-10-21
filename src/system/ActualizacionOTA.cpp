#include "ActualizacionOTA.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "../config.h"
#include "../core/Sha256.h"
#include "Watchdog.h"

// El core marca la aplicación como válida al arrancar salvo que esta función
// devuelva true: así la decisión es del autodiagnóstico (core::evaluarArranque).
extern "C" bool verifyRollbackLater() { return true; }

// Ningún tramo sin alimentar el watchdog puede superar el que ya fija su
// plazo: handshake TLS y luego cada espera de datos por separado.
static const uint32_t OTA_HANDSHAKE_MS = TLS_HANDSHAKE_TIMEOUT_MS;
static const uint32_t OTA_ESPERA_DATOS_MS = MQTT_COMMAND_TIMEOUT_MS;
static_assert(OTA_HANDSHAKE_MS + OTA_ESPERA_DATOS_MS <= PEOR_BLOQUEO_NO_ALIMENTABLE_MS,
              "La descarga OTA bloquearia mas que el plazo del watchdog");
static const char* const CLAVE_DESTINO_OTA = "ota_dest";

static void olvidarDestinoOTA() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return;
    nvs.remove(CLAVE_DESTINO_OTA);
    nvs.end();
}

const char* nombreResultadoOTA(ResultadoOTA r) {
    switch (r) {
        case ResultadoOTA::Instalada:          return "instalada";
        case ResultadoOTA::VersionNoPosterior: return "version_no_posterior";
        case ResultadoOTA::SinCA:              return "sin_ca";
        case ResultadoOTA::Descarga:           return "descarga";
        case ResultadoOTA::TamanoDistinto:     return "tamano_distinto";
        case ResultadoOTA::HuellaDistinta:     return "huella_distinta";
        default:                               return "escritura";
    }
}

ResultadoOTA instalarOTA(const core::OrdenOTA& orden, const char* caPem) {
    core::VersionFirmware actual, nueva;
    actual.mayor = FW_MAYOR;
    actual.menor = FW_MENOR;
    actual.parche = FW_PARCHE;
    if (!core::parsearVersion(orden.version, nueva) || !core::esPosterior(nueva, actual)) {
        return ResultadoOTA::VersionNoPosterior;
    }
    if (caPem == nullptr || caPem[0] == '\0') return ResultadoOTA::SinCA;

    WiFiClientSecure tls;
    tls.setCACert(caPem);
    tls.setHandshakeTimeout(OTA_HANDSHAKE_MS / 1000);
    HTTPClient http;
    http.setConnectTimeout(OTA_HANDSHAKE_MS);
    http.setTimeout(OTA_ESPERA_DATOS_MS);
    // Una redirección podría llevar a otro host: la URL de la orden es la única.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, orden.url.c_str())) return ResultadoOTA::Descarga;
    alimentarWatchdog();
    if (http.GET() != HTTP_CODE_OK) {
        http.end();
        return ResultadoOTA::Descarga;
    }
    if (http.getSize() != (int)orden.tamano) {
        http.end();
        return ResultadoOTA::TamanoDistinto;
    }
    const esp_partition_t* destino = esp_ota_get_next_update_partition(nullptr);
    if (destino == nullptr || !Update.begin(orden.tamano, U_FLASH)) {
        http.end();
        return ResultadoOTA::Escritura;
    }

    NetworkClient* flujo = http.getStreamPtr();
    core::Sha256 huella;
    uint8_t trozo[1024];
    uint32_t restante = orden.tamano;
    while (restante > 0) {
        alimentarWatchdog();
        const size_t pedir = restante < sizeof(trozo) ? restante : sizeof(trozo);
        const size_t leidos = flujo->readBytes(trozo, pedir);
        if (leidos == 0) {
            Update.abort();
            http.end();
            return ResultadoOTA::Descarga;
        }
        huella.actualizar(trozo, leidos);
        if (Update.write(trozo, leidos) != leidos) {
            Update.abort();
            http.end();
            return ResultadoOTA::Escritura;
        }
        restante -= (uint32_t)leidos;
    }
    http.end();

    // La imagen no se activa si no es EXACTAMENTE la anunciada por el backend.
    if (!core::igualesTiempoConstante(huella.finalizarHex(), orden.sha256)) {
        Update.abort();
        return ResultadoOTA::HuellaDistinta;
    }
    if (!Update.end()) return ResultadoOTA::Escritura;  // con Secure Boot verifica la firma
    // Se recuerda a qué partición se instaló: si tras reiniciar el nodo vuelve
    // a correr en la otra, la imagen nueva fue revertida.
    Preferences nvs;
    if (nvs.begin(NVS_NAMESPACE, false)) {
        nvs.putUInt(CLAVE_DESTINO_OTA, destino->address);
        nvs.end();
    }
    return ResultadoOTA::Instalada;
}

bool imagenEnPrueba() {
    esp_ota_img_states_t estado;
    const esp_partition_t* enEjecucion = esp_ota_get_running_partition();
    return enEjecucion != nullptr && esp_ota_get_state_partition(enEjecucion, &estado) == ESP_OK &&
           estado == ESP_OTA_IMG_PENDING_VERIFY;
}

void confirmarImagen() {
    esp_ota_mark_app_valid_cancel_rollback();
    olvidarDestinoOTA();
}

void revertirImagen() { esp_ota_mark_app_invalid_rollback_and_reboot(); }

const char* particionEnEjecucion() {
    const esp_partition_t* p = esp_ota_get_running_partition();
    return p != nullptr ? p->label : "?";
}

bool consumirAvisoReversion() {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return false;
    const uint32_t destino = nvs.getUInt(CLAVE_DESTINO_OTA, 0);
    nvs.end();
    const esp_partition_t* enEjecucion = esp_ota_get_running_partition();
    if (destino == 0 || enEjecucion == nullptr || enEjecucion->address == destino) return false;
    // Hubo una OTA a `destino` pero el nodo corre en la otra partición: la
    // imagen nueva no superó el arranque a prueba y el bootloader la revirtió.
    olvidarDestinoOTA();
    return true;
}
