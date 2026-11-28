#ifndef CORE_CERTIFICADO_H
#define CORE_CERTIFICADO_H

#include <cstdint>

namespace core {

/**
 * HU-09 esc. 2: vigencia del certificado del broker.
 *
 * El core de Arduino-ESP32 3.3.12 compila mbedTLS sin
 * `CONFIG_MBEDTLS_HAVE_TIME_DATE`: la validación de la cadena comprueba la CA y
 * el nombre del servidor, pero NO las fechas `notBefore`/`notAfter`. Un
 * certificado vencido pasaba el handshake. El nodo lo comprueba aquí con la
 * hora SNTP, antes de enviar el CONNECT (que lleva el token).
 *
 * Sin hora fiable (`epoch` < 2024-01-01) no se puede decidir: se devuelve
 * `SinHora` y el llamador vuelve a comprobarlo cuando llegue SNTP.
 */

/// Fecha UTC tal como la entrega X.509 (`mbedtls_x509_time`): mes 1–12.
struct FechaX509 {
    int anio = 0;
    int mes = 0;
    int dia = 0;
    int hora = 0;
    int minuto = 0;
    int segundo = 0;
};

enum class VigenciaCertificado : uint8_t {
    Vigente,
    SinHora,        ///< el nodo aún no tiene hora SNTP: no se puede decidir
    AunNoValido,    ///< ahora < notBefore
    Vencido,        ///< ahora > notAfter
    FechaInvalida,  ///< fecha fuera de rango o notBefore > notAfter
};

const char* nombreVigencia(VigenciaCertificado vigencia);

/// true si la fecha es una fecha civil válida (años 1950–9999, RFC 5280).
bool fechaX509Valida(const FechaX509& fecha);

/// Segundos desde 1970-01-01T00:00:00Z. Sin `timegm()`/`mktime()` (hora local
/// y estado global). La fecha debe ser válida (`fechaX509Valida`).
int64_t epochDesdeFechaX509(const FechaX509& fecha);

/// Vigencia de un certificado en `epochAhora`. El período es inclusivo en
/// ambos extremos (RFC 5280 §4.1.2.5).
VigenciaCertificado evaluarVigencia(const FechaX509& desde, const FechaX509& hasta, uint32_t epochAhora);

/// Para recorrer una cadena: devuelve la peor de las dos
/// (FechaInvalida > Vencido > AunNoValido > SinHora > Vigente).
VigenciaCertificado peorVigencia(VigenciaCertificado a, VigenciaCertificado b);

/// true si la vigencia obliga a rechazar la conexión.
bool vigenciaRechaza(VigenciaCertificado vigencia);

}  // namespace core

#endif  // CORE_CERTIFICADO_H
