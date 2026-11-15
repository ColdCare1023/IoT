#include "Reconexion.h"

#include "Json.h"
#include "Registro.h"

namespace core {

static void ponerU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static uint32_t leerU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char* nombreEtapaReconexion(EtapaReconexion etapa) {
    switch (etapa) {
        case EtapaReconexion::WiFi:   return "wifi";
        case EtapaReconexion::Broker: return "broker";
        default:                      return nullptr;
    }
}

// Formato (little-endian, 44 bytes):
//   0-3 'R''C''N''1'   4 banderas (bit0 activo, bit1 diagnóstico)   5 etapa
//   8 intentos  12 reinicios  16/20/24 desde (boot, uptime, época)
//   28/32/36 hasta (boot, uptime, época)  40 CRC32 de los bytes 0..39
void codificarEpisodio(const EpisodioReconexion& e, uint8_t s[TAM_EPISODIO_RECONEXION]) {
    for (size_t i = 0; i < TAM_EPISODIO_RECONEXION; ++i) s[i] = 0;
    s[0] = 'R';
    s[1] = 'C';
    s[2] = 'N';
    s[3] = '1';
    s[4] = (uint8_t)((e.activo ? 0x01 : 0) | (e.diagnostico ? 0x02 : 0));
    s[5] = (uint8_t)e.etapa;
    ponerU32(&s[8], e.intentos);
    ponerU32(&s[12], e.reinicios);
    ponerU32(&s[16], e.desde.bootId);
    ponerU32(&s[20], e.desde.uptimeS);
    ponerU32(&s[24], e.desde.epoch);
    ponerU32(&s[28], e.hasta.bootId);
    ponerU32(&s[32], e.hasta.uptimeS);
    ponerU32(&s[36], e.hasta.epoch);
    ponerU32(&s[40], crc32(s, 40));
}

bool decodificarEpisodio(const uint8_t s[TAM_EPISODIO_RECONEXION], EpisodioReconexion& e) {
    if (s[0] != 'R' || s[1] != 'C' || s[2] != 'N' || s[3] != '1') return false;
    if (crc32(s, 40) != leerU32(&s[40])) return false;
    if (s[5] > (uint8_t)EtapaReconexion::Broker) return false;
    EpisodioReconexion r;
    r.activo = (s[4] & 0x01) != 0;
    r.diagnostico = (s[4] & 0x02) != 0;
    r.etapa = (EtapaReconexion)s[5];
    r.intentos = leerU32(&s[8]);
    r.reinicios = leerU32(&s[12]);
    r.desde.bootId = leerU32(&s[16]);
    r.desde.uptimeS = leerU32(&s[20]);
    r.desde.epoch = leerU32(&s[24]);
    r.hasta.bootId = leerU32(&s[28]);
    r.hasta.uptimeS = leerU32(&s[32]);
    r.hasta.epoch = leerU32(&s[36]);
    e = r;
    return true;
}

SeguimientoReconexion::SeguimientoReconexion(PersistenciaReconexion& persistencia, uint32_t umbralIntentos,
                                             uint32_t intervaloPersistenciaS)
    : _persistencia(persistencia),
      _umbral(umbralIntentos == 0 ? 1 : umbralIntentos),
      _intervaloS(intervaloPersistenciaS) {}

void SeguimientoReconexion::montar(uint32_t bootId) {
    uint8_t buf[TAM_EPISODIO_RECONEXION];
    EpisodioReconexion leido;
    if (!_persistencia.leer(buf, sizeof(buf)) || !decodificarEpisodio(buf, leido)) {
        _episodio = EpisodioReconexion();  // nada guardado o ilegible: sin episodio
        return;
    }
    _episodio = leido;
    if (_episodio.activo) {
        // El nodo reinició con el episodio abierto (p. ej. TASK_WDT): se
        // cuenta y se guarda ya, por si vuelve a reiniciar.
        _episodio.reinicios++;
        MarcaTiempo ahora;
        ahora.bootId = bootId;
        _guardar(ahora);
    }
}

void SeguimientoReconexion::marcarDesconexion(const MarcaTiempo& ahora) {
    if (_episodio.activo) return;
    _hayDesconexion = true;
    _desconexion = ahora;
}

bool SeguimientoReconexion::registrarFallo(EtapaReconexion etapa, const MarcaTiempo& ahora) {
    bool forzar = false;
    if (!_episodio.activo) {
        EpisodioReconexion nuevo;
        nuevo.activo = true;
        nuevo.desde = _hayDesconexion ? _desconexion : ahora;
        _episodio = nuevo;
        _hayDesconexion = false;
        forzar = true;
    }
    _episodio.intentos++;
    _episodio.etapa = etapa;
    // Si el episodio empezó sin hora y en este mismo arranque, la hora llega
    // al cerrarlo (retrofechado con el uptime); aquí no se inventa.
    bool cruza = false;
    if (!_episodio.diagnostico && _episodio.intentos >= _umbral) {
        _episodio.diagnostico = true;
        cruza = true;
        forzar = true;
    }
    const bool vencido = !_hayGuardado || _ultimoGuardado.bootId != ahora.bootId ||
                         ahora.uptimeS - _ultimoGuardado.uptimeS >= _intervaloS;
    if (forzar || vencido) _guardar(ahora);
    return cruza;
}

bool SeguimientoReconexion::registrarConexion(const MarcaTiempo& ahora, EpisodioReconexion& cerrado) {
    _hayDesconexion = false;
    if (!_episodio.activo) return false;
    _episodio.activo = false;
    _episodio.hasta = ahora;
    _guardar(ahora);
    cerrado = _episodio;
    return _episodio.diagnostico || _episodio.reinicios > 0;
}

void SeguimientoReconexion::_guardar(const MarcaTiempo& ahora) {
    uint8_t buf[TAM_EPISODIO_RECONEXION];
    codificarEpisodio(_episodio, buf);
    _persistencia.escribir(buf, sizeof(buf));
    _escrituras++;
    _hayGuardado = true;
    _ultimoGuardado = ahora;
}

bool epochDeMarca(const MarcaTiempo& m, const BaseTiempo& base, uint32_t& epoch) {
    return resolverTiempo(m.epoch, m.bootId, m.uptimeS, base, epoch) != CalidadTiempo::SinSincronizar;
}

bool duracionEpisodioS(const EpisodioReconexion& e, const BaseTiempo& base, uint32_t& duracionS) {
    uint32_t d = 0, h = 0;
    if (epochDeMarca(e.desde, base, d) && epochDeMarca(e.hasta, base, h)) {
        duracionS = h >= d ? h - d : 0;
        return true;
    }
    if (e.desde.bootId == e.hasta.bootId) {
        duracionS = e.hasta.uptimeS >= e.desde.uptimeS ? e.hasta.uptimeS - e.desde.uptimeS : 0;
        return true;
    }
    return false;
}

std::string detalleReconexionProlongada(const EpisodioReconexion& e, uint32_t umbral, const BaseTiempo& base) {
    ObjetoJSON d;
    d.sinSigno("intentos_fallidos", e.intentos);
    uint32_t duracion = 0;
    if (duracionEpisodioS(e, base, duracion)) d.sinSigno("duracion_s", duracion);
    else                                      d.nulo("duracion_s");
    d.sinSigno("umbral_intentos", umbral);
    uint32_t epoch = 0;
    if (epochDeMarca(e.desde, base, epoch)) d.cadena("desde", formatearISO8601(epoch));
    else                                    d.nulo("desde");
    if (!e.activo && epochDeMarca(e.hasta, base, epoch)) d.cadena("hasta", formatearISO8601(epoch));
    else                                                 d.nulo("hasta");
    d.sinSigno("reinicios", e.reinicios);
    const char* etapa = nombreEtapaReconexion(e.etapa);
    if (etapa != nullptr) d.cadena("etapa", etapa);
    else                  d.nulo("etapa");
    return d.cerrar();
}

}  // namespace core
