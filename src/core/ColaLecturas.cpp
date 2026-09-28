#include "ColaLecturas.h"

#include <algorithm>
#include <cstring>

namespace core {

static const char* const META_CURSOR = "cursor";
static const size_t TAM_CURSOR = 20;
static const size_t TAM_ENTRADA_CUARENTENA = 4 + TAM_REGISTRO;

static void ponerU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t leerU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

ColaLecturas::ColaLecturas(AlmacenSegmentos& almacen, PersistenciaPerdidas& perdidas,
                           const ConfigCola& config)
    : _almacen(almacen), _persistencia(perdidas), _config(config) {
    if (_config.registrosPorSegmento == 0) _config.registrosPorSegmento = 1;
    if (_config.maxSegmentos == 0) _config.maxSegmentos = 1;
}

// =========================================================================
// Montaje / recuperación tras reinicio (HU-06 escenario 2)
// =========================================================================

InformeMontaje ColaLecturas::montar() {
    InformeMontaje informe;
    _segmentos.clear();
    _cursorIndice = 0;

    std::vector<uint32_t> ids = _almacen.listarSegmentos();
    std::sort(ids.begin(), ids.end());

    uint32_t mayorId = 0;
    for (uint32_t id : ids) {
        if (id > mayorId) mayorId = id;
        const size_t tam = _almacen.tamanoSegmento(id);
        const uint32_t registros = (uint32_t)(tam / TAM_REGISTRO);
        const size_t resto = tam % TAM_REGISTRO;

        if (resto != 0) {
            // Escritura interrumpida (corte de energía a mitad de anexar):
            // se ignora solo la cola incompleta y el segmento se sella para
            // que ningún registro nuevo quede desalineado detrás de ella.
            informe.segmentosConColaParcial++;
            informe.bytesParcialesIgnorados += (uint32_t)resto;
        }
        if (registros == 0) {
            // Sin ningún registro completo: no hay nada recuperable.
            _almacen.borrarSegmento(id);
            informe.segmentosVaciosBorrados++;
            continue;
        }
        Segmento s;
        s.id = id;
        s.registros = registros;
        s.sellado = (resto != 0) || registros >= _config.registrosPorSegmento;
        _segmentos.push_back(s);
    }
    // Solo el último segmento puede seguir abierto.
    for (size_t i = 0; i + 1 < _segmentos.size(); ++i) _segmentos[i].sellado = true;

    // ── Cursor ───────────────────────────────────────────────────────────
    uint8_t buf[TAM_CURSOR];
    bool cursorOk = _almacen.leerMeta(META_CURSOR, buf, TAM_CURSOR) && buf[0] == 'C' &&
                    buf[1] == 'R' && buf[2] == 'S' && buf[3] == '1' &&
                    crc32(buf, 16) == leerU32(&buf[16]);
    uint32_t cursorSeg = 0, cursorIdx = 0, siguienteGuardado = 1;
    if (cursorOk) {
        cursorSeg = leerU32(&buf[4]);
        cursorIdx = leerU32(&buf[8]);
        siguienteGuardado = leerU32(&buf[12]);
    }
    _siguienteId = std::max<uint32_t>(std::max<uint32_t>(siguienteGuardado, mayorId + 1), 1);

    if (!_segmentos.empty()) {
        if (!cursorOk) {
            // Sin cursor fiable se reenvía desde el principio: el backend
            // deduplica por (device_id, boot_id, seq_no). Duplicar es
            // inofensivo; perder no.
            informe.cursorReconstruido = true;
            _cursorIndice = 0;
        } else {
            // Segmentos anteriores al cursor ya tenían acuse de todo: el
            // corte ocurrió entre el acuse y su borrado.
            while (!_segmentos.empty() && _segmentos[0].id < cursorSeg) {
                _almacen.borrarSegmento(_segmentos[0].id);
                _segmentos.erase(_segmentos.begin());
            }
            if (!_segmentos.empty() && _segmentos[0].id == cursorSeg) {
                _cursorIndice = std::min(cursorIdx, _segmentos[0].registros);
            } else {
                _cursorIndice = 0;
            }
        }
        _normalizarCabeza();
    }
    _persistirCursor();

    // ── Pérdidas persistentes ────────────────────────────────────────────
    if (!_persistencia.cargar(_perdidas)) _perdidas = EstadoPerdidas();
    // Un episodio abierto al reiniciar se vuelve a reportar: puede que el
    // nodo nunca llegara a avisarlo antes del corte.
    if (_perdidas.episodioAbierto) _avisoPerdidasPendiente = true;

    _avisoArmado = true;
    _evaluarUmbral();

    informe.segmentos = (uint32_t)_segmentos.size();
    informe.pendientes = pendientes();
    return informe;
}

// =========================================================================
// Escritura (Core de sensores)
// =========================================================================

bool ColaLecturas::_hayEspacioParaSegmentoNuevo() {
    return _segmentos.size() < _config.maxSegmentos;
}

ResultadoGuardado ColaLecturas::guardar(const RegistroLectura& registro) {
    uint8_t buf[TAM_REGISTRO];
    codificarRegistro(registro, buf);

    // Margen del sistema de archivos: sin él LittleFS no puede ni hacer el
    // copy-on-write del último bloque ni actualizar el cursor.
    if (_almacen.bytesLibres() < _config.reservaBytesLibres) {
        _registrarPerdida(registro);
        return ResultadoGuardado::SaturadoNoAlmacenado;
    }

    const bool hayCola = !_segmentos.empty() && !_segmentos.back().sellado &&
                         _segmentos.back().registros < _config.registrosPorSegmento;
    bool nuevo = false;
    if (!hayCola) {
        if (!_hayEspacioParaSegmentoNuevo()) {
            // 100 %: se preservan las lecturas antiguas; la nueva no entra.
            _registrarPerdida(registro);
            return ResultadoGuardado::SaturadoNoAlmacenado;
        }
        Segmento s;
        s.id = _siguienteId++;
        s.registros = 0;
        s.sellado = false;
        _segmentos.push_back(s);
        nuevo = true;
    }

    Segmento& cola = _segmentos.back();
    const size_t escritos = _almacen.anexar(cola.id, buf, TAM_REGISTRO);
    if (escritos != TAM_REGISTRO) {
        _erroresEscritura++;
        if (nuevo && escritos == 0) {
            _almacen.borrarSegmento(cola.id);
            _segmentos.pop_back();
        } else {
            cola.sellado = true;  // cola desalineada: nada más se escribe aquí
        }
        _registrarPerdida(registro);
        return ResultadoGuardado::ErrorEscritura;
    }

    cola.registros++;
    if (cola.registros >= _config.registrosPorSegmento) cola.sellado = true;

    _cerrarEpisodioSiAbierto();
    _evaluarUmbral();
    return ResultadoGuardado::Guardado;
}

void ColaLecturas::_registrarPerdida(const RegistroLectura& r) {
    const uint32_t epoch = r.tiene(BANDERA_TIEMPO_SNTP) ? r.capturadoEpoch : 0;
    _perdidas.total++;
    if (!_perdidas.episodioAbierto) {
        _perdidas.episodioAbierto = true;
        _perdidas.episodio = 0;
        _perdidas.desdeBoot = r.bootId;
        _perdidas.desdeSeq = r.seqNo;
        _perdidas.desdeEpoch = epoch;
    }
    _perdidas.episodio++;
    _perdidas.hastaBoot = r.bootId;
    _perdidas.hastaSeq = r.seqNo;
    _perdidas.hastaEpoch = epoch;
    _persistencia.guardar(_perdidas);
    _avisoPerdidasPendiente = true;
}

void ColaLecturas::_cerrarEpisodioSiAbierto() {
    if (!_perdidas.episodioAbierto) return;
    _perdidas.episodioAbierto = false;
    _persistencia.guardar(_perdidas);
    _avisoPerdidasPendiente = true;  // se reporta el cierre con el periodo final
}

// =========================================================================
// Lectura y liberación (Core de red)
// =========================================================================

void ColaLecturas::_normalizarCabeza() {
    while (!_segmentos.empty()) {
        const Segmento& cabeza = _segmentos[0];
        if (!(cabeza.sellado && _cursorIndice >= cabeza.registros)) break;
        _almacen.borrarSegmento(cabeza.id);
        _segmentos.erase(_segmentos.begin());
        _cursorIndice = 0;
    }
}

EstadoCabeza ColaLecturas::leerCabeza(RegistroLectura& registro, uint8_t bruto[TAM_REGISTRO]) {
    _normalizarCabeza();
    if (_segmentos.empty()) return EstadoCabeza::Vacia;
    const Segmento& cabeza = _segmentos[0];
    if (_cursorIndice >= cabeza.registros) return EstadoCabeza::Vacia;

    const size_t desplazamiento = (size_t)_cursorIndice * TAM_REGISTRO;
    bool leido = _almacen.leer(cabeza.id, desplazamiento, bruto, TAM_REGISTRO);
    if (!leido) leido = _almacen.leer(cabeza.id, desplazamiento, bruto, TAM_REGISTRO);  // un reintento
    if (!leido) {
        std::memset(bruto, 0, TAM_REGISTRO);
        return EstadoCabeza::Corrupta;
    }
    return decodificarRegistro(bruto, registro) ? EstadoCabeza::Ok : EstadoCabeza::Corrupta;
}

void ColaLecturas::_avanzarCursor() {
    _cursorIndice++;
    _normalizarCabeza();
    _evaluarUmbral();
}

bool ColaLecturas::confirmarCabeza() {
    if (_segmentos.empty() || _cursorIndice >= _segmentos[0].registros) return false;
    _avanzarCursor();
    return _persistirCursor();
}

bool ColaLecturas::cuarentenarCabeza(MotivoCuarentena motivo, const uint8_t bruto[TAM_REGISTRO]) {
    if (_segmentos.empty() || _cursorIndice >= _segmentos[0].registros) return false;

    uint8_t entrada[TAM_ENTRADA_CUARENTENA] = {0};
    entrada[0] = (uint8_t)motivo;
    std::memcpy(&entrada[4], bruto, TAM_REGISTRO);

    const size_t ocupadas = _almacen.tamanoCuarentena() / TAM_ENTRADA_CUARENTENA;
    if (ocupadas >= _config.maxRegistrosCuarentena ||
        _almacen.anexarCuarentena(entrada, TAM_ENTRADA_CUARENTENA) != TAM_ENTRADA_CUARENTENA) {
        _cuarentenaDesbordada++;
    } else {
        _cuarentenados++;
    }
    _avanzarCursor();
    return _persistirCursor();
}

bool ColaLecturas::_persistirCursor() {
    uint8_t buf[TAM_CURSOR];
    buf[0] = 'C';
    buf[1] = 'R';
    buf[2] = 'S';
    buf[3] = '1';
    ponerU32(&buf[4], _segmentos.empty() ? _siguienteId : _segmentos[0].id);
    ponerU32(&buf[8], _cursorIndice);
    ponerU32(&buf[12], _siguienteId);
    ponerU32(&buf[16], crc32(buf, 16));
    return _almacen.escribirMeta(META_CURSOR, buf, TAM_CURSOR);
}

// =========================================================================
// Estado y avisos
// =========================================================================

uint32_t ColaLecturas::pendientes() const {
    uint32_t total = 0;
    for (const auto& s : _segmentos) total += s.registros;
    return total >= _cursorIndice ? total - _cursorIndice : 0;
}

uint32_t ColaLecturas::capacidadRegistros() const {
    return _config.maxSegmentos * _config.registrosPorSegmento;
}

uint8_t ColaLecturas::ocupacionPct() const {
    // Ocupación FÍSICA: un registro con acuse sigue ocupando su segmento
    // hasta que el segmento entero se libera.
    uint64_t usados = 0;
    for (const auto& s : _segmentos) {
        usados += s.sellado ? _config.registrosPorSegmento : s.registros;
    }
    const uint64_t capacidad = capacidadRegistros();
    if (capacidad == 0) return 100;
    const uint64_t pct = (usados * 100u) / capacidad;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

void ColaLecturas::_evaluarUmbral() {
    const uint8_t pct = ocupacionPct();
    if (_avisoArmado && pct >= _config.umbralAvisoPct) {
        _avisoArmado = false;
        _avisoPendiente = true;
        _pctAviso = pct;
    } else if (!_avisoArmado && pct < _config.umbralRearmePct) {
        _avisoArmado = true;
    }
}

bool ColaLecturas::consumirAvisoOcupacion(uint8_t& pct) {
    if (!_avisoPendiente) return false;
    _avisoPendiente = false;
    pct = _pctAviso;
    return true;
}

bool ColaLecturas::consumirAvisoPerdidas(EstadoPerdidas& estado) {
    if (!_avisoPerdidasPendiente) return false;
    _avisoPerdidasPendiente = false;
    estado = _perdidas;
    return true;
}

// =========================================================================
// Drenaje
// =========================================================================

namespace {
/// RAII: garantiza `soltar()` en todos los caminos de salida.
class Seccion {
public:
    explicit Seccion(Cerrojo* c) : _c(c) { if (_c != nullptr) _c->tomar(); }
    ~Seccion() { if (_c != nullptr) _c->soltar(); }
    Seccion(const Seccion&) = delete;
    Seccion& operator=(const Seccion&) = delete;

private:
    Cerrojo* _c;
};
}  // namespace

ResumenDrenaje drenar(ColaLecturas& cola, Transmisor& transmisor, int maxPorCiclo,
                      Cerrojo* cerrojo) {
    ResumenDrenaje r;
    for (int operaciones = 0; operaciones < maxPorCiclo; ++operaciones) {
        RegistroLectura registro;
        uint8_t bruto[TAM_REGISTRO];
        EstadoCabeza estado;
        {
            Seccion s(cerrojo);
            estado = cola.leerCabeza(registro, bruto);
            if (estado == EstadoCabeza::Corrupta) {
                cola.cuarentenarCabeza(MotivoCuarentena::CrcInvalido, bruto);
                r.corruptos++;
                continue;
            }
        }
        if (estado == EstadoCabeza::Vacia) break;

        // Fuera del cerrojo: aquí se espera el PUBACK y el acuse lógico.
        const ResultadoEnvio resultado = transmisor.enviar(registro);

        Seccion s(cerrojo);
        switch (resultado) {
            case ResultadoEnvio::Confirmado:
                cola.confirmarCabeza();
                r.confirmados++;
                break;
            case ResultadoEnvio::Rechazado:
                cola.cuarentenarCabeza(MotivoCuarentena::RechazadoBackend, bruto);
                r.rechazados++;
                break;
            case ResultadoEnvio::Invalido:
                cola.cuarentenarCabeza(MotivoCuarentena::PayloadInvalido, bruto);
                r.invalidos++;
                break;
            case ResultadoEnvio::Fallo:
            case ResultadoEnvio::SinConexion:
            default:
                r.detenido = true;
                return r;
        }
    }
    return r;
}

}  // namespace core
