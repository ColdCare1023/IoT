#include "BufferLittleFS.h"

#include <LittleFS.h>
#include <Preferences.h>

#include <cstdlib>

#include "../config.h"

static const char* const DIR_BASE = "/tt";
static const char* const DIR_COLA = "/tt/q";
static const char* const RUTA_CUARENTENA = "/tt/cuarentena.bin";

static core::ConfigCola configCola() {
    core::ConfigCola c;
    c.registrosPorSegmento = BUFFER_REGISTROS_POR_SEGMENTO;
    c.maxSegmentos = BUFFER_MAX_SEGMENTOS;
    c.umbralAvisoPct = BUFFER_UMBRAL_AVISO_PCT;
    c.umbralRearmePct = BUFFER_UMBRAL_REARME_PCT;
    c.maxRegistrosCuarentena = BUFFER_MAX_CUARENTENA;
    c.reservaBytesLibres = BUFFER_RESERVA_BYTES;
    return c;
}

static String rutaSegmento(uint32_t id) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s/%08lu.bin", DIR_COLA, (unsigned long)id);
    return String(buf);
}

static String rutaMeta(const char* nombre) {
    return String(DIR_BASE) + "/" + nombre + ".bin";
}

// =========================================================================
// Adaptador LittleFS
// =========================================================================

std::vector<uint32_t> BufferLittleFS::AlmacenLittleFS::listarSegmentos() {
    std::vector<uint32_t> ids;
    File dir = LittleFS.open(DIR_COLA);
    if (!dir || !dir.isDirectory()) return ids;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            // name() es el nombre base en el core 2.0.x: "00000042.bin".
            const char* nombre = f.name();
            const char* base = strrchr(nombre, '/');
            base = base != nullptr ? base + 1 : nombre;
            char* fin = nullptr;
            const unsigned long id = strtoul(base, &fin, 10);
            if (fin != nullptr && fin - base == 8 && strcmp(fin, ".bin") == 0 && id > 0) {
                ids.push_back((uint32_t)id);
            }
        }
        f.close();  // sin esto se agotan los descriptores de la VFS
        f = dir.openNextFile();
    }
    dir.close();
    return ids;
}

size_t BufferLittleFS::AlmacenLittleFS::tamanoSegmento(uint32_t id) {
    const String ruta = rutaSegmento(id);
    if (!LittleFS.exists(ruta)) return 0;
    File f = LittleFS.open(ruta, "r");
    if (!f) return 0;
    const size_t t = f.size();
    f.close();
    return t;
}

size_t BufferLittleFS::AlmacenLittleFS::anexar(uint32_t id, const uint8_t* datos, size_t longitud) {
    File f = LittleFS.open(rutaSegmento(id), "a");
    if (!f) return 0;
    const size_t escritos = f.write(datos, longitud);
    f.close();  // LittleFS confirma los datos al cerrar
    return escritos;
}

bool BufferLittleFS::AlmacenLittleFS::leer(uint32_t id, size_t desplazamiento, uint8_t* datos,
                                           size_t longitud) {
    File f = LittleFS.open(rutaSegmento(id), "r");
    if (!f) return false;
    bool ok = f.seek(desplazamiento) && f.read(datos, longitud) == longitud;
    f.close();
    return ok;
}

bool BufferLittleFS::AlmacenLittleFS::borrarSegmento(uint32_t id) {
    return LittleFS.remove(rutaSegmento(id));
}

bool BufferLittleFS::AlmacenLittleFS::leerMeta(const char* nombre, uint8_t* datos, size_t longitud) {
    const String ruta = rutaMeta(nombre);
    if (!LittleFS.exists(ruta)) return false;
    File f = LittleFS.open(ruta, "r");
    if (!f) return false;
    const bool ok = f.size() == longitud && f.read(datos, longitud) == longitud;
    f.close();
    return ok;
}

bool BufferLittleFS::AlmacenLittleFS::escribirMeta(const char* nombre, const uint8_t* datos,
                                                   size_t longitud) {
    File f = LittleFS.open(rutaMeta(nombre), "w");
    if (!f) return false;
    const bool ok = f.write(datos, longitud) == longitud;
    f.close();
    return ok;
}

size_t BufferLittleFS::AlmacenLittleFS::tamanoCuarentena() {
    if (!LittleFS.exists(RUTA_CUARENTENA)) return 0;
    File f = LittleFS.open(RUTA_CUARENTENA, "r");
    if (!f) return 0;
    const size_t t = f.size();
    f.close();
    return t;
}

size_t BufferLittleFS::AlmacenLittleFS::anexarCuarentena(const uint8_t* datos, size_t longitud) {
    File f = LittleFS.open(RUTA_CUARENTENA, "a");
    if (!f) return 0;
    const size_t escritos = f.write(datos, longitud);
    f.close();
    return escritos;
}

size_t BufferLittleFS::AlmacenLittleFS::bytesLibres() {
    const size_t total = LittleFS.totalBytes();
    const size_t usados = LittleFS.usedBytes();
    return total > usados ? total - usados : 0;
}

// =========================================================================
// Contador persistente de pérdidas (NVS)
// =========================================================================

static void ponerU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}
static uint32_t leerU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool BufferLittleFS::PerdidasNVS::cargar(core::EstadoPerdidas& e) {
    uint8_t b[44];
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, true)) return false;
    const size_t n = nvs.getBytes("perdidas", b, sizeof(b));
    nvs.end();
    if (n != sizeof(b) || b[0] != 'P' || b[1] != 'E' || b[2] != 'R' || b[3] != '1') return false;
    if (core::crc32(b, 40) != leerU32(&b[40])) return false;
    e.total = leerU32(&b[4]);
    e.episodio = leerU32(&b[8]);
    e.episodioAbierto = leerU32(&b[12]) != 0;
    e.desdeBoot = leerU32(&b[16]);
    e.desdeSeq = leerU32(&b[20]);
    e.desdeEpoch = leerU32(&b[24]);
    e.hastaBoot = leerU32(&b[28]);
    e.hastaSeq = leerU32(&b[32]);
    e.hastaEpoch = leerU32(&b[36]);
    return true;
}

bool BufferLittleFS::PerdidasNVS::guardar(const core::EstadoPerdidas& e) {
    uint8_t b[44];
    b[0] = 'P'; b[1] = 'E'; b[2] = 'R'; b[3] = '1';
    ponerU32(&b[4], e.total);
    ponerU32(&b[8], e.episodio);
    ponerU32(&b[12], e.episodioAbierto ? 1u : 0u);
    ponerU32(&b[16], e.desdeBoot);
    ponerU32(&b[20], e.desdeSeq);
    ponerU32(&b[24], e.desdeEpoch);
    ponerU32(&b[28], e.hastaBoot);
    ponerU32(&b[32], e.hastaSeq);
    ponerU32(&b[36], e.hastaEpoch);
    ponerU32(&b[40], core::crc32(b, 40));
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = nvs.putBytes("perdidas", b, sizeof(b)) == sizeof(b);
    nvs.end();
    return ok;
}

// =========================================================================
// BufferLittleFS
// =========================================================================

BufferLittleFS::BufferLittleFS()
    : _mutex(xSemaphoreCreateRecursiveMutex()),
      _cerrojo(_mutex),
      _cola(_almacen, _perdidas, configCola()) {}

bool BufferLittleFS::begin(bool& formateado, core::InformeMontaje& informe) {
    formateado = false;
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);

    if (!LittleFS.begin(false, "/littlefs", 10, LITTLEFS_ETIQUETA_PARTICION)) {
        LOG_E("Buffer", "No se pudo montar LittleFS: se formatea la particion.");
        if (!LittleFS.begin(true, "/littlefs", 10, LITTLEFS_ETIQUETA_PARTICION)) {
            xSemaphoreGiveRecursive(_mutex);
            return false;
        }
        formateado = true;
    }
    if (!LittleFS.exists(DIR_BASE)) LittleFS.mkdir(DIR_BASE);
    if (!LittleFS.exists(DIR_COLA)) LittleFS.mkdir(DIR_COLA);

    informe = _cola.montar();
    xSemaphoreGiveRecursive(_mutex);

    LOG_I("Buffer", "Montado: %lu pendientes en %lu segmentos, %u%% ocupado, %u KB libres.",
          (unsigned long)informe.pendientes, (unsigned long)informe.segmentos,
          (unsigned)ocupacionPct(), (unsigned)(bytesLibres() / 1024));
    if (informe.segmentosConColaParcial > 0) {
        LOG_E("Buffer", "%lu segmento(s) con escritura interrumpida: %lu bytes incompletos ignorados.",
              (unsigned long)informe.segmentosConColaParcial,
              (unsigned long)informe.bytesParcialesIgnorados);
    }
    if (informe.cursorReconstruido) {
        LOG_E("Buffer", "Cursor ilegible: se reenviara desde el inicio (el backend deduplica).");
    }
    return true;
}

core::ResultadoGuardado BufferLittleFS::guardar(const core::RegistroLectura& registro) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    const core::ResultadoGuardado r = _cola.guardar(registro);
    xSemaphoreGiveRecursive(_mutex);
    return r;
}

core::ResumenDrenaje BufferLittleFS::drenar(core::Transmisor& transmisor, int maxPorCiclo) {
    return core::drenar(_cola, transmisor, maxPorCiclo, &_cerrojo);
}

#define CON_MUTEX(expr)                                   \
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);       \
    auto _valor = (expr);                                 \
    xSemaphoreGiveRecursive(_mutex);                      \
    return _valor

uint32_t BufferLittleFS::pendientes() { CON_MUTEX(_cola.pendientes()); }
uint8_t BufferLittleFS::ocupacionPct() { CON_MUTEX(_cola.ocupacionPct()); }
uint32_t BufferLittleFS::capacidad() { CON_MUTEX(_cola.capacidadRegistros()); }
uint32_t BufferLittleFS::cuarentenados() { CON_MUTEX(_cola.cuarentenados()); }
uint32_t BufferLittleFS::cuarentenaDesbordada() { CON_MUTEX(_cola.cuarentenaDesbordada()); }
uint32_t BufferLittleFS::erroresEscritura() { CON_MUTEX(_cola.erroresEscritura()); }
core::EstadoPerdidas BufferLittleFS::perdidas() { CON_MUTEX(_cola.perdidas()); }
size_t BufferLittleFS::bytesLibres() { CON_MUTEX(_almacen.bytesLibres()); }

bool BufferLittleFS::consumirAvisoOcupacion(uint8_t& pct) {
    CON_MUTEX(_cola.consumirAvisoOcupacion(pct));
}
bool BufferLittleFS::consumirAvisoPerdidas(core::EstadoPerdidas& estado) {
    CON_MUTEX(_cola.consumirAvisoPerdidas(estado));
}

#undef CON_MUTEX
