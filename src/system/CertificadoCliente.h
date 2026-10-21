#ifndef SYSTEM_CERTIFICADO_CLIENTE_H
#define SYSTEM_CERTIFICADO_CLIENTE_H

#include <Arduino.h>

/**
 * IOT-006: clave y certificado de cliente del nodo en NVS (`mqtt_key`,
 * `mqtt_cert`; cifradas con NVS Encryption en producción). La clave se genera
 * aquí y nunca se muestra; por la consola solo salen la CSR y el estado.
 *
 * Aprovisionamiento (consola desbloqueada, Wi-Fi configurado):
 *   tls csr                    → genera la clave (si no hay) e imprime la CSR
 *   (la CA de dispositivos firma la CSR con CN = device_id y uso clientAuth)
 *   tls certificado <base64>   → instala el certificado (DER en base64, una línea)
 */

/// Certificado y clave guardados y coherentes con `deviceId`; si no, false.
bool cargarCertificadoCliente(const String& deviceId, String& certPem, String& clavePem);

enum class ResultadoCsr : uint8_t { Generada, SinDeviceId, SinRadio, Fallo };
/// `nuevaClave` descarta la clave anterior (y su certificado).
ResultadoCsr prepararCsr(const String& deviceId, bool nuevaClave, String& csrPem, bool& claveNueva);

/// Valida el certificado contra la clave y el device_id antes de guardarlo.
/// `motivo` es el nombre del rechazo ("" si se instaló).
bool instalarCertificadoCliente(const String& deviceId, const String& base64Der, String& motivo);

/// Borra certificado y clave (la clave sin su certificado no sirve y al revés).
bool borrarCertificadoCliente();

#endif  // SYSTEM_CERTIFICADO_CLIENTE_H
