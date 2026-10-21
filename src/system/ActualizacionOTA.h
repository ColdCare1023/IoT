#ifndef SYSTEM_ACTUALIZACION_OTA_H
#define SYSTEM_ACTUALIZACION_OTA_H

#include <Arduino.h>

#include "../core/Ota.h"

/**
 * IOT-004: actualización OTA verificada y arranque a prueba.
 *
 * La imagen se descarga por HTTPS (CA del nodo, sin seguir redirecciones),
 * se comprueba el tamaño anunciado y la huella SHA-256 MIENTRAS se escribe en
 * la partición OTA inactiva, y solo entonces se marca como siguiente arranque.
 * Con Secure Boot (IOT-001) `Update.end()` exige además la firma.
 *
 * Tras reiniciar, la imagen nueva queda A PRUEBA: el nodo la confirma cuando
 * captura una lectura y conecta con el broker; si no lo logra en el plazo, la
 * invalida y el bootloader vuelve a la versión anterior.
 */

enum class ResultadoOTA : uint8_t {
    Instalada,
    VersionNoPosterior,
    SinCA,
    Descarga,
    TamanoDistinto,
    HuellaDistinta,
    Escritura,
};

const char* nombreResultadoOTA(ResultadoOTA resultado);

/// Descarga, verifica e instala. Con `Instalada` hay que reiniciar.
ResultadoOTA instalarOTA(const core::OrdenOTA& orden, const char* caPem);

/// ¿La imagen en ejecución acaba de instalarse por OTA y aún no se confirmó?
bool imagenEnPrueba();
void confirmarImagen();
/// Invalida la imagen en prueba y reinicia en la anterior (no vuelve).
void revertirImagen();

/// Etiqueta de la partición en ejecución ("app0"/"app1").
const char* particionEnEjecucion();

/// true una sola vez por reversión: el arranque posterior a una OTA revertida.
bool consumirAvisoReversion();

#endif  // SYSTEM_ACTUALIZACION_OTA_H
