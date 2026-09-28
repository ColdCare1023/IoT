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
 *   device_id                                    — reemplaza DEVICE_ID
 *   intervalo_s                                  — intervalo de muestreo (10..3600)
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

    bool credencialesCompletas() const;
};

ConfiguracionNodo cargarConfiguracion();

/// Relee solo el token (HU-44: rotación sin reiniciar). "" si no hay.
String releerTokenMqtt();

/// Lee boot_id de NVS, lo incrementa, lo persiste y lo devuelve (≥1).
/// Devuelve 0 si NVS no está disponible (degradado: la captura sigue).
uint32_t cargarEIncrementarBootId();

/// Escritura desde la consola. Solo claves de la lista blanca.
bool guardarClaveNvs(const String& clave, const String& valor);
bool borrarClaveNvs(const String& clave);

#endif  // SYSTEM_CONFIGURACION_H
