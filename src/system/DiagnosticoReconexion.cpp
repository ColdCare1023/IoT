#include "DiagnosticoReconexion.h"

#include <Preferences.h>

#include "../config.h"
#include "Reloj.h"

static const char* const CLAVE_NVS = "reconexion";

bool ReconexionNVS::leer(uint8_t* datos, size_t longitud) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return false;
    const size_t n = nvs.isKey(CLAVE_NVS) ? nvs.getBytes(CLAVE_NVS, datos, longitud) : 0;
    nvs.end();
    return n == longitud;
}

bool ReconexionNVS::escribir(const uint8_t* datos, size_t longitud) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = nvs.putBytes(CLAVE_NVS, datos, longitud) == longitud;
    nvs.end();
    if (!ok) LOG_E("WiFi", "No se pudo guardar el episodio de reconexion en NVS.");
    return ok;
}

core::SeguimientoReconexion& diagnosticoReconexion() {
    static ReconexionNVS persistencia;
    static core::SeguimientoReconexion seguimiento(persistencia, WIFI_UMBRAL_DIAGNOSTICO_INTENTOS,
                                                   RECONEXION_INTERVALO_PERSISTENCIA_S);
    return seguimiento;
}

core::MarcaTiempo marcaAhora(uint32_t bootId) {
    core::MarcaTiempo m;
    m.bootId = bootId;
    m.uptimeS = uptimeSegundos();
    m.epoch = epochAhora();
    return m;
}
