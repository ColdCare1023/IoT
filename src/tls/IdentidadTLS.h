#ifndef TLS_IDENTIDAD_TLS_H
#define TLS_IDENTIDAD_TLS_H

#include <string>

/**
 * IOT-006: identidad X.509 del nodo para TLS mutuo con el broker.
 *
 * El token MQTT es un secreto portátil: quien lo copia puede suplantar al nodo
 * desde cualquier equipo. Con un certificado de cliente, la clave privada se
 * GENERA EN EL NODO (curva P-256), no sale de él (NVS, cifrada con NVS
 * Encryption en producción, IOT-001) y solo se exporta la solicitud de firma
 * (CSR) con CN = device_id. La CA de dispositivos firma la CSR fuera del nodo y
 * el certificado se instala por la consola.
 *
 * Solo mbedTLS (sin Arduino): se compila igual en el ESP32 y en el equipo de
 * desarrollo para probarlo contra OpenSSL.
 */
namespace tls {

/// Genera una clave EC P-256 nueva (PEM). En el ESP32 la entropía es la del RNG
/// por hardware: con la radio encendida (Wi-Fi iniciado) es aleatoria de verdad.
bool generarClavePem(std::string& clavePem);

/// CSR firmada con la clave, sujeto "CN=<deviceId>" (el device_id ya validado).
bool generarCsrPem(const std::string& clavePem, const std::string& deviceId, std::string& csrPem);

enum class ResultadoCertificado {
    Valido,
    NoEsCertificado,   ///< no se pudo leer como X.509
    ClaveInvalida,     ///< la clave guardada no se pudo leer
    ClaveNoCoincide,   ///< el certificado es de OTRA clave
    OtroNodo,          ///< el CN no es este device_id (o hay más de un CN)
    NoEsDeCliente,     ///< tiene extendedKeyUsage y no incluye clientAuth
};
const char* nombreResultado(ResultadoCertificado r);

/// ¿El certificado corresponde a la clave del nodo y a su device_id?
/// La vigencia no se comprueba: mbedTLS no la valida sin reloj (ADR-12) y la
/// revocación efectiva la aplica el backend (BE-002).
ResultadoCertificado validarCertificado(const std::string& certPem, const std::string& clavePem,
                                        const std::string& deviceId);

}  // namespace tls

#endif  // TLS_IDENTIDAD_TLS_H
