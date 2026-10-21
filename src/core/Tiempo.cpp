#include "Tiempo.h"

#include <cstdio>

namespace core {

const char* nombreCalidadTiempo(CalidadTiempo calidad) {
    switch (calidad) {
        case CalidadTiempo::Sincronizado: return "sntp_synced";
        case CalidadTiempo::Retrofechado: return "sntp_backfilled";
        case CalidadTiempo::SinSincronizar:
        default:                          return "unsynced";
    }
}

CalidadTiempo resolverTiempo(uint32_t epochCaptura, uint32_t bootIdLectura,
                             uint32_t uptimeLecturaS, const BaseTiempo& base,
                             uint32_t& epochSalida) {
    if (epochCaptura >= EPOCH_MINIMA_VALIDA) {
        epochSalida = epochCaptura;
        return CalidadTiempo::Sincronizado;
    }
    if (base.valida && base.bootId == bootIdLectura) {
        const int64_t estimada = (int64_t)uptimeLecturaS + base.desplazamientoS;
        if (estimada >= (int64_t)EPOCH_MINIMA_VALIDA && estimada <= 0xFFFFFFFFll) {
            epochSalida = (uint32_t)estimada;
            return CalidadTiempo::Retrofechado;
        }
    }
    epochSalida = 0;
    return CalidadTiempo::SinSincronizar;
}

// Algoritmo civil_from_days de Howard Hinnant (válido en todo el calendario
// gregoriano proléptico, sin estado global).
static void civilDesdeDias(int64_t dias, int& anio, unsigned& mes, unsigned& dia) {
    dias += 719468;
    const int64_t era = (dias >= 0 ? dias : dias - 146096) / 146097;
    const unsigned doe = (unsigned)(dias - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    dia = doy - (153 * mp + 2) / 5 + 1;
    mes = (unsigned)((int)mp + (mp < 10 ? 3 : -9));
    anio = (int)(y + (mes <= 2 ? 1 : 0));
}

std::string formatearISO8601(uint32_t epoch) {
    const int64_t dias = (int64_t)(epoch / 86400u);
    const unsigned resto = (unsigned)(epoch % 86400u);
    int anio;
    unsigned mes, dia;
    civilDesdeDias(dias, anio, mes, dia);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02u:%02u:%02uZ", anio, mes, dia,
             resto / 3600u, (resto % 3600u) / 60u, resto % 60u);
    return std::string(buf);
}

DecisionSincronizacion evaluarSincronizacion(const BaseTiempo& vigente, uint32_t bootId, uint32_t epoch,
                                             uint32_t uptimeS, uint32_t toleranciaS) {
    if (epoch < EPOCH_MINIMA_VALIDA || epoch > EPOCH_MAXIMA_PLAUSIBLE) {
        return DecisionSincronizacion::FueraDeRango;
    }
    if (!vigente.valida || vigente.bootId != bootId) return DecisionSincronizacion::Aceptar;
    const int64_t nuevo = (int64_t)epoch - (int64_t)uptimeS;
    const int64_t salto = nuevo - vigente.desplazamientoS;
    const int64_t magnitud = salto < 0 ? -salto : salto;
    return magnitud > (int64_t)toleranciaS ? DecisionSincronizacion::SaltoRechazado
                                           : DecisionSincronizacion::Aceptar;
}

}  // namespace core
