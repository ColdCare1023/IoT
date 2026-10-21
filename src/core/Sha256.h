#ifndef CORE_SHA256_H
#define CORE_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

/**
 * SHA-256 (FIPS 180-4) portable, sin dependencias del SDK, para que la lógica
 * que lo usa se pruebe en el host (`pio test -e native`):
 *  - IOT-002: la clave de servicio de la consola se guarda solo como huella.
 *  - IOT-008: el acuse lógico se coteja con la huella del JSON enviado.
 */
std::string sha256Hex(const uint8_t* datos, size_t largo);
std::string sha256Hex(const std::string& texto);

/// SHA-256 por partes, para datos que llegan en trozos (IOT-004: la imagen
/// OTA se verifica mientras se descarga, sin tenerla entera en RAM).
class Sha256 {
public:
    Sha256();
    void actualizar(const uint8_t* datos, size_t largo);
    /// Cierra el cálculo y devuelve la huella; el objeto queda reiniciado.
    std::string finalizarHex();

private:
    void _reiniciar();
    uint32_t _h[8];
    uint8_t _bloque[64];
    size_t _enBloque = 0;
    uint64_t _total = 0;
};

/// Comparación en tiempo constante (no se corta en la primera diferencia).
bool igualesTiempoConstante(const std::string& a, const std::string& b);

}  // namespace core

#endif  // CORE_SHA256_H
