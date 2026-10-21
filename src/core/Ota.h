#ifndef CORE_OTA_H
#define CORE_OTA_H

#include <cstdint>
#include <string>

namespace core {

/**
 * IOT-004: reglas portables de la actualización OTA.
 *
 * El nodo solo se actualizaba por USB, sin verificación ni vuelta atrás. La
 * orden OTA llega del backend por MQTT (`farmacias/<device_id>/ota`, tópico
 * que solo el backend puede publicar) y se acepta únicamente si:
 *   - es para ESTE nodo, con URL HTTPS y huella SHA-256 de la imagen;
 *   - la versión es POSTERIOR a la que corre (sin degradaciones);
 *   - la imagen descargada tiene exactamente esa huella.
 * Con Secure Boot (IOT-001) el bootloader exige además la firma, y el
 * anti-rollback por eFuse impide volver a versiones de seguridad anteriores.
 * La imagen nueva arranca A PRUEBA: si no supera el autodiagnóstico en el
 * plazo, el bootloader vuelve a la anterior.
 */

struct VersionFirmware {
    uint16_t mayor = 0;
    uint16_t menor = 0;
    uint16_t parche = 0;
};

/// "1.2.0" (sin prefijo ni sufijos). false si no es una versión válida.
bool parsearVersion(const std::string& texto, VersionFirmware& version);
bool esPosterior(const VersionFirmware& candidata, const VersionFirmware& actual);

constexpr uint32_t OTA_TAMANO_MAXIMO = 0x130000;  // partición de aplicación (1216 KB)

struct OrdenOTA {
    std::string version;
    std::string url;
    std::string sha256;
    uint32_t tamano = 0;
};

/// Valida la orden recibida. `motivo` explica el rechazo (sin datos sensibles).
bool parsearOrdenOTA(const std::string& json, const std::string& deviceId, OrdenOTA& orden,
                     std::string& motivo);

/// Autodiagnóstico de una imagen recién instalada (arranque "a prueba").
enum class DecisionArranque : uint8_t { Esperar, Confirmar, Revertir };

/// Se confirma cuando el nodo capturó una lectura y conectó con el broker;
/// si no lo logra en `plazoMs`, se revierte. Sin imagen a prueba: Confirmar.
DecisionArranque evaluarArranque(bool imagenAPrueba, bool capturoLectura, bool conectoBroker,
                                 uint32_t msDesdeArranque, uint32_t plazoMs);

}  // namespace core

#endif  // CORE_OTA_H
