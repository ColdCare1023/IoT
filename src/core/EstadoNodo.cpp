#include "EstadoNodo.h"

#include "Json.h"

namespace core {

bool instanteDeRegistro(const RegistroLectura& r, const BaseTiempo& base, uint32_t& epoch) {
    const uint32_t epochCaptura = r.tiene(BANDERA_TIEMPO_SNTP) ? r.capturadoEpoch : 0;
    return resolverTiempo(epochCaptura, r.bootId, r.uptimeS, base, epoch) != CalidadTiempo::SinSincronizar;
}

static void instanteOnulo(ObjetoJSON& o, const char* clave, bool hay, const RegistroLectura& r,
                          const BaseTiempo& base) {
    uint32_t epoch = 0;
    if (hay && instanteDeRegistro(r, base, epoch)) o.cadena(clave, formatearISO8601(epoch));
    else                                          o.nulo(clave);
}

std::string detalleEstadoNodo(const DatosEstadoNodo& d, const BaseTiempo& base) {
    ObjetoJSON o;
    o.sinSigno("pendientes", d.pendientes);
    o.sinSigno("ocupacion_pct", d.ocupacionPct);
    o.sinSigno("capacidad", d.capacidad);
    // B medido en el nodo: tamaño útil de LittleFS y lo que queda libre.
    o.sinSigno("fs_total_bytes", d.fsTotalBytes);
    o.sinSigno("fs_libres_bytes", d.fsLibresBytes);
    o.sinSigno("perdidas_total", d.perdidasTotal);
    o.sinSigno("cuarentena", d.cuarentena);
    o.sinSigno("cuarentena_desbordada", d.cuarentenaDesbordada);
    o.sinSigno("ultimo_seq_no", d.ultimoSeqNo);
    o.entero("rssi_dbm", d.rssiDbm);
    o.sinSigno("heap_libre", d.heapLibre);
    o.sinSigno("heap_minimo", d.heapMinimo);
    o.sinSigno("intervalo_s", d.intervaloS);
    o.sinSigno("pubacks_sin_ack", d.pubacksSinAck);
    o.sinSigno("acks_huella_ajena", d.acksHuellaAjena);  // IOT-008
    o.sinSigno("eventos_descartados", d.eventosDescartados);
    o.booleano("reloj_sntp", d.relojSntp);
    o.sinSigno("saltos_reloj_rechazados", d.saltosRelojRechazados);  // IOT-007

    // HU-48 esc. 1: rango real de los pendientes (sin pendientes, null).
    const bool hay = d.pendientes > 0;
    instanteOnulo(o, "pendientes_desde", hay && d.hayPrimerPendiente, d.primerPendiente, base);
    instanteOnulo(o, "pendientes_hasta", hay && d.hayUltimoPendiente, d.ultimoPendiente, base);

    // HU-08 esc. 3: episodio de reconexión (persistente).
    const EpisodioReconexion& e = d.reconexion;
    o.sinSigno("reconexion_intentos", e.intentos);
    uint32_t epoch = 0;
    if (e.intentos > 0 && epochDeMarca(e.desde, base, epoch)) o.cadena("reconexion_desde", formatearISO8601(epoch));
    else                                                     o.nulo("reconexion_desde");
    if (e.intentos > 0 && !e.activo && epochDeMarca(e.hasta, base, epoch)) {
        o.cadena("reconexion_hasta", formatearISO8601(epoch));
    } else {
        o.nulo("reconexion_hasta");
    }
    o.sinSigno("reconexion_reinicios", e.reinicios);
    const char* etapa = nombreEtapaReconexion(e.etapa);
    if (etapa != nullptr) o.cadena("reconexion_etapa", etapa);
    else                  o.nulo("reconexion_etapa");

    // HU-04: bitácora de puerta persistente.
    o.sinSigno("puerta_pendientes", d.puertaPendientes);
    o.sinSigno("puerta_descartados", d.puertaDescartados);

    // HU-02: recuperaciones del bus del SHT31.
    o.sinSigno("sht31_recuperaciones", d.sht31Recuperaciones);
    return o.cerrar();
}

std::string detalleDesconexionOrdenada(const char* motivo) {
    ObjetoJSON o;
    o.cadena("motivo", motivo != nullptr ? motivo : "desconocido");
    return o.cerrar();
}

}  // namespace core
