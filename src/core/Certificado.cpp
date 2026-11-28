#include "Certificado.h"

#include "Tiempo.h"

namespace core {

const char* nombreVigencia(VigenciaCertificado vigencia) {
    switch (vigencia) {
        case VigenciaCertificado::Vigente:       return "vigente";
        case VigenciaCertificado::SinHora:       return "sin_hora";
        case VigenciaCertificado::AunNoValido:   return "aun_no_valido";
        case VigenciaCertificado::Vencido:       return "vencido";
        case VigenciaCertificado::FechaInvalida:
        default:                                 return "fecha_invalida";
    }
}

static bool esBisiesto(int anio) { return (anio % 4 == 0 && anio % 100 != 0) || anio % 400 == 0; }

static int diasDelMes(int anio, int mes) {
    static const int dias[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (mes == 2 && esBisiesto(anio)) ? 29 : dias[mes - 1];
}

bool fechaX509Valida(const FechaX509& f) {
    if (f.anio < 1950 || f.anio > 9999) return false;
    if (f.mes < 1 || f.mes > 12) return false;
    if (f.dia < 1 || f.dia > diasDelMes(f.anio, f.mes)) return false;
    if (f.hora < 0 || f.hora > 23) return false;
    if (f.minuto < 0 || f.minuto > 59) return false;
    return f.segundo >= 0 && f.segundo <= 59;
}

// Algoritmo days_from_civil de Howard Hinnant (inverso de civilDesdeDias en
// Tiempo.cpp): gregoriano proléptico, sin estado global.
static int64_t diasDesdeCivil(int64_t anio, unsigned mes, unsigned dia) {
    anio -= mes <= 2 ? 1 : 0;
    const int64_t era = (anio >= 0 ? anio : anio - 399) / 400;
    const unsigned yoe = (unsigned)(anio - era * 400);
    const unsigned doy = (153 * (mes > 2 ? mes - 3 : mes + 9) + 2) / 5 + dia - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t epochDesdeFechaX509(const FechaX509& f) {
    const int64_t dias = diasDesdeCivil(f.anio, (unsigned)f.mes, (unsigned)f.dia);
    return dias * 86400 + (int64_t)f.hora * 3600 + (int64_t)f.minuto * 60 + f.segundo;
}

VigenciaCertificado evaluarVigencia(const FechaX509& desde, const FechaX509& hasta, uint32_t epochAhora) {
    if (!fechaX509Valida(desde) || !fechaX509Valida(hasta)) return VigenciaCertificado::FechaInvalida;
    const int64_t inicio = epochDesdeFechaX509(desde);
    const int64_t fin = epochDesdeFechaX509(hasta);
    if (inicio > fin) return VigenciaCertificado::FechaInvalida;
    if (epochAhora < EPOCH_MINIMA_VALIDA) return VigenciaCertificado::SinHora;
    const int64_t ahora = (int64_t)epochAhora;
    if (ahora < inicio) return VigenciaCertificado::AunNoValido;
    if (ahora > fin) return VigenciaCertificado::Vencido;
    return VigenciaCertificado::Vigente;
}

VigenciaCertificado peorVigencia(VigenciaCertificado a, VigenciaCertificado b) {
    return (uint8_t)a >= (uint8_t)b ? a : b;
}

bool vigenciaRechaza(VigenciaCertificado vigencia) {
    return vigencia == VigenciaCertificado::AunNoValido || vigencia == VigenciaCertificado::Vencido ||
           vigencia == VigenciaCertificado::FechaInvalida;
}

}  // namespace core
