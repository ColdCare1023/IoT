#ifndef CORE_POSESION_H
#define CORE_POSESION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Registro.h"

namespace core {

/**
 * HU-44: prueba de posesión de la credencial tras una rotación por compromiso.
 *
 * Cambiar la contraseña en la consola de EMQX Serverless es un paso manual.
 * Si el administrador confirma en el sistema sin haberla cambiado, el broker
 * sigue aceptando el token filtrado. Por eso el backend no sale de la
 * retención con la confirmación, sino cuando el nodo firma un desafío con el
 * token de SU SESIÓN ACTUAL:
 *
 *   clave     = SHA-256("coldcare-pop-v1:" + token)
 *   respuesta = HMAC-SHA256(clave, nonce + ":" + device_id)   (hex)
 *
 * Si el broker rechazó el relevo y el nodo volvió al token anterior, firma con
 * ese token y la respuesta no coincide. Quien solo tiene el token filtrado
 * tampoco puede responder. El backend guarda la clave derivada, nunca el token.
 */
std::string hmacSha256Hex(const uint8_t* clave, size_t largoClave, const std::string& mensaje);

std::string respuestaPosesion(const std::string& token, const std::string& deviceId,
                              const std::string& nonce);

/// Desafío publicado por el backend en `.../ack`:
/// {"device_id":"...","tipo":"desafio_posesion","nonce":"<hex 16-64>"}.
/// Devuelve false si no es un desafío o no es para este nodo.
bool parsearDesafioPosesion(const std::string& json, const std::string& deviceId, std::string& nonce);

/// HU-44: muestra de la última captura para el evento `lectura_vigente`.
/// Mientras el backend no acusa (retención por compromiso), el drenaje FIFO
/// solo reintenta la lectura más antigua; sin esta muestra el backend no vería
/// una excursión que ocurra durante la ventana. No sustituye a la telemetría:
/// el backend no la persiste como lectura.
std::string detalleLecturaVigente(const RegistroLectura& r);

}  // namespace core

#endif  // CORE_POSESION_H
