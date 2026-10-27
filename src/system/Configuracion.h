#ifndef SYSTEM_CONFIGURACION_H
#define SYSTEM_CONFIGURACION_H

#include <Arduino.h>

/**
 * Configuración de ejecución del nodo (RNF-05, HU-43, HU-44).
 *
 * Precedencia: NVS > build_flags > vacío. NVS permite aprovisionar cada nodo
 * sin recompilar ni dejar el token en ningún archivo del repositorio; se
 * escribe desde la consola serie (`nvs set ...`, ver system/Consola.h).
 *
 * Claves NVS (namespace "thermotrace"):
 *   wifi_ssid, wifi_pass, mqtt_host, mqtt_token  — credenciales
 *   mqtt_relevo                                  — token nuevo cargado ANTES de cambiar el
 *                                                  broker (HU-44, core::debeUsarRelevo)
 *   device_id                                    — reemplaza DEVICE_ID (no en el firmware demo)
 *   intervalo_s                                  — intervalo de muestreo (10..3600)
 *   svc_hash                                     — huella de la clave de servicio (IOT-002)
 *   mqtt_key, mqtt_cert                          — identidad TLS del nodo (IOT-006,
 *                                                  system/CertificadoCliente.h)
 *   boot_id                                      — contador de arranques
 *   perdidas                                     — contador persistente (blob)
 */
struct ConfiguracionNodo {
    String deviceId;
    String wifiSsid;
    String wifiPassword;
    String mqttHost;
    String mqttToken;
    uint32_t intervaloMuestreoS = 30;
    String origen;  ///< de dónde salieron las credenciales (nunca los valores)
    /// IOT-005: firmware demo sobre un nodo con identidad productiva en NVS.
    /// El nodo captura pero NO publica (ver core/Identidad.h).
    bool identidadBloqueada = false;

    bool credencialesCompletas() const;
};

ConfiguracionNodo cargarConfiguracion();

/// Relee solo el token (HU-44: rotación sin reiniciar). "" si no hay.
String releerTokenMqtt();

/// HU-44 (A-1): token de relevo ("" si no hay o no es válido).
String leerTokenRelevo();
/// Lo vuelve vigente (`mqtt_token`) y borra el relevo. False si NVS falla.
bool promoverTokenRelevo(const String& relevo);
/// Borra el relevo (ya es el token vigente).
void descartarTokenRelevo();

/// Lee boot_id de NVS, lo incrementa, lo persiste y lo devuelve (≥1).
/// Devuelve 0 si NVS no está disponible (degradado: la captura sigue).
uint32_t cargarEIncrementarBootId();

/// Escritura desde la consola. Solo claves de la lista blanca.
/// `clave_servicio` (IOT-002) se guarda solo como huella SHA-256.
bool guardarClaveNvs(const String& clave, const String& valor);
bool borrarClaveNvs(const String& clave);

/// IOT-002: huella de la clave de servicio de la consola ("" si no hay).
String huellaClaveServicio();

#endif  // SYSTEM_CONFIGURACION_H
