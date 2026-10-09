#include "Payload.h"

#include <cstdio>

#include "Json.h"

namespace core {

std::string textoVersionFirmware(uint8_t mayor, uint8_t menor, uint8_t parche) {
    char buf[20];
    snprintf(buf, sizeof(buf), "v%u.%u.%u", (unsigned)mayor, (unsigned)menor, (unsigned)parche);
    return std::string(buf);
}

bool deviceIdValido(const std::string& deviceId) {
    if (deviceId.empty() || deviceId.size() > DEVICE_ID_MAX) return false;
    for (char c : deviceId) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/// ¿El par valor/fallo es coherente? Valor presente ⇔ sin fallo.
static bool coherente(bool hayValor, FalloSensor fallo) {
    return hayValor == (fallo == FalloSensor::Ninguno);
}

bool validarRegistro(const RegistroLectura& r, std::string& motivo) {
    if (r.seqNo == 0) {
        motivo = "seq_no_cero";
        return false;
    }
    if (!coherente(r.tempDsCenti != NULO_I16, r.falloDs)) {
        motivo = "ds18b20_incoherente";
        return false;
    }
    if (!coherente(r.tempShtCenti != NULO_I16, r.falloShtT) ||
        !coherente(r.humShtCenti != NULO_U16, r.falloShtH)) {
        motivo = "sht31_incoherente";
        return false;
    }
    if (r.humShtCenti != NULO_U16 && r.humShtCenti > 10000) {
        motivo = "humedad_fuera_de_rango";
        return false;
    }
    if (r.tempDsCenti != NULO_I16 && (r.tempDsCenti < -5500 || r.tempDsCenti > 12500)) {
        motivo = "ds18b20_fuera_de_rango";
        return false;
    }
    if (r.tempShtCenti != NULO_I16 && (r.tempShtCenti < -4000 || r.tempShtCenti > 12500)) {
        motivo = "sht31_fuera_de_rango";
        return false;
    }
    if (!r.tiene(BANDERA_MC38_INSTALADO) &&
        (r.tiene(BANDERA_PUERTA_ABIERTA) || r.puertaAbiertaS != 0 || r.puertaRachaS != 0)) {
        motivo = "puerta_sin_mc38";
        return false;
    }
    motivo.clear();
    return true;
}

std::string serializarTelemetria(const RegistroLectura& r, const std::string& deviceId,
                                 const BaseTiempo& base, size_t maxBytes) {
    std::string motivo;
    if (!deviceIdValido(deviceId) || !validarRegistro(r, motivo)) return std::string();

    ObjetoJSON o;
    o.cadena("schema_version", SCHEMA_VERSION_TELEMETRIA);
    o.cadena("device_id", deviceId);
    o.sinSigno("boot_id", r.bootId);
    o.sinSigno("seq_no", r.seqNo);

    uint32_t epoch = 0;
    const uint32_t epochCaptura = r.tiene(BANDERA_TIEMPO_SNTP) ? r.capturadoEpoch : 0;
    const CalidadTiempo calidad = resolverTiempo(epochCaptura, r.bootId, r.uptimeS, base, epoch);
    if (calidad == CalidadTiempo::SinSincronizar) {
        o.nulo("captured_at");
    } else {
        o.cadena("captured_at", formatearISO8601(epoch));
    }
    o.cadena("time_quality", nombreCalidadTiempo(calidad));
    o.sinSigno("uptime_s", r.uptimeS);

    // ── DS18B20 (sonda interior) ──────────────────────────────────────────
    const bool dsOk = r.falloDs == FalloSensor::Ninguno;
    if (dsOk) o.real("temp_ds18b20", deCentesimas(r.tempDsCenti), 2);
    else      o.nulo("temp_ds18b20");
    o.cadena("ds18b20_status", dsOk ? "OK" : "SENSOR_FAULT");
    if (dsOk) o.nulo("ds18b20_fault");
    else      o.cadena("ds18b20_fault", codigoFallo(r.falloDs));

    // ── SHT31 (ambiente exterior) ─────────────────────────────────────────
    const bool tOk = r.falloShtT == FalloSensor::Ninguno;
    const bool hOk = r.falloShtH == FalloSensor::Ninguno;
    if (tOk) o.real("temp_sht31", deCentesimas(r.tempShtCenti), 2);
    else     o.nulo("temp_sht31");
    if (hOk) o.real("humedad_sht31", deCentesimasSinSigno(r.humShtCenti), 2);
    else     o.nulo("humedad_sht31");
    o.cadena("sht31_status", (tOk && hOk) ? "OK" : "SENSOR_FAULT");
    if (!tOk)      o.cadena("sht31_fault", codigoFallo(r.falloShtT));
    else if (!hOk) o.cadena("sht31_fault", codigoFallo(r.falloShtH));
    else           o.nulo("sht31_fault");

    // ── MC-38 (opcional) ──────────────────────────────────────────────────
    if (r.tiene(BANDERA_MC38_INSTALADO)) {
        o.booleano("door_open", r.tiene(BANDERA_PUERTA_ABIERTA));
        o.sinSigno("door_open_s", r.puertaAbiertaS);
        if (r.puertaRachaS > 0) o.sinSigno("door_open_streak_s", r.puertaRachaS);
        else                    o.nulo("door_open_streak_s");
        o.cadena("mc38_status", "installed");
    } else {
        o.nulo("door_open");
        o.nulo("door_open_s");
        o.nulo("door_open_streak_s");
        o.cadena("mc38_status", "not_installed");
    }

    // ── Energía (solo si hay sensado de suministro instalado) ─────────────
    if (r.tiene(BANDERA_SENSADO_ENERGIA)) {
        o.cadena("power_source", r.tiene(BANDERA_EN_RESPALDO) ? "backup" : "mains");
    } else {
        o.nulo("power_source");
    }

    o.booleano("sensor_fault", !dsOk || !tOk || !hOk);
    o.cadena("firmware_version", textoVersionFirmware(r.fwMayor, r.fwMenor, r.fwParche));
    if (r.tiene(BANDERA_SIMULADO)) o.booleano("simulated", true);

    const std::string& json = o.cerrar();
    if (json.size() > maxBytes) return std::string();
    return json;
}

std::string serializarEvento(const std::string& deviceId, uint32_t bootId,
                             const char* tipoEvento, uint32_t epochEvento,
                             uint32_t uptimeEventoS, const BaseTiempo& base,
                             const std::string& detalleJSON) {
    ObjetoJSON o;
    o.cadena("schema_version", SCHEMA_VERSION_TELEMETRIA);
    o.cadena("device_id", deviceId);
    o.sinSigno("boot_id", bootId);
    o.cadena("event_type", tipoEvento != nullptr ? tipoEvento : "desconocido");

    uint32_t epoch = 0;
    const CalidadTiempo calidad = resolverTiempo(epochEvento, bootId, uptimeEventoS, base, epoch);
    if (calidad == CalidadTiempo::SinSincronizar) o.nulo("occurred_at");
    else                                         o.cadena("occurred_at", formatearISO8601(epoch));
    o.cadena("time_quality", nombreCalidadTiempo(calidad));
    o.sinSigno("uptime_s", uptimeEventoS);
    o.crudo("detail", detalleJSON);
    return o.cerrar();
}

std::string serializarLWT(const std::string& deviceId, uint32_t bootId) {
    ObjetoJSON o;
    o.cadena("schema_version", SCHEMA_VERSION_TELEMETRIA);
    o.cadena("device_id", deviceId);
    o.sinSigno("boot_id", bootId);
    o.cadena("event_type", "lwt_offline");
    return o.cerrar();
}

}  // namespace core
