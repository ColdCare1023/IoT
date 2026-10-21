#ifndef CORE_PEM_H
#define CORE_PEM_H

#include <cstddef>
#include <string>

namespace core {

/**
 * IOT-006: el certificado del nodo llega por la consola serie, que trabaja por
 * líneas: se teclea como base64 del DER en UNA línea y aquí se valida y se
 * convierte al PEM que espera mbedTLS (líneas de 64 caracteres).
 */

/// Largo máximo aceptado del base64 (un certificado de nodo con su firma cabe
/// holgado; también acota la línea de la consola).
constexpr size_t PEM_BASE64_MAXIMO = 1500;

/// Base64 estándar sin espacios: alfabeto válido, largo múltiplo de 4, relleno
/// `=` solo al final (0, 1 o 2) y no vacío.
bool base64Valido(const std::string& b64, size_t maxLargo = PEM_BASE64_MAXIMO);

/// "-----BEGIN <etiqueta>-----\n" + base64 en líneas de 64 + "-----END <etiqueta>-----\n".
/// "" si el base64 no es válido.
std::string pemDesdeBase64(const std::string& etiqueta, const std::string& b64);

}  // namespace core

#endif  // CORE_PEM_H
