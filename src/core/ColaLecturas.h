#ifndef CORE_COLA_LECTURAS_H
#define CORE_COLA_LECTURAS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Ack.h"
#include "Registro.h"

namespace core {

/**
 * Buffer offline FIFO de lecturas (HU-06 / HU-07 / HU-48), sin tocar flash.
 *
 * ESTRUCTURA EN LITTLEFS
 * ----------------------
 *   /tt/q/00000001.bin   segmento más antiguo: hasta N registros de 40 B
 *   /tt/q/00000002.bin
 *   ...
 *   /tt/cursor.bin       próximo registro sin acuse lógico (+ CRC)
 *   /tt/cuarentena.bin   registros rechazados o corruptos (nunca se borran en silencio)
 *
 * Solo se escribe al final del último segmento. Se libera un segmento
 * completo solo cuando TODOS sus registros recibieron acuse lógico (o fueron
 * movidos a cuarentena). Nada se sobrescribe.
 *
 * POLÍTICAS
 * ---------
 *  - 80 %: aviso una sola vez al cruzar el umbral (se rearma bajo el 70 %).
 *  - 100 %: la lectura nueva NO se guarda; se preservan las antiguas y se
 *    incrementa un contador PERSISTENTE de muestras no almacenadas, con el
 *    periodo afectado (boot_id/seq_no/época inicial y final). Ninguna pérdida
 *    queda silenciosa y el hueco se ve además como salto en `seq_no`.
 *  - Reinicio: se recuperan los segmentos existentes; un segmento con cola
 *    parcial (escritura interrumpida por un corte) se sella y se lee solo
 *    hasta el último registro completo. Un registro con CRC inválido no se
 *    publica: se copia a cuarentena con su motivo y la cola sigue.
 *
 * El acceso real a LittleFS y la exclusión mutua entre núcleos viven en
 * `storage/BufferLittleFS`; esta clase se prueba en el host con un almacén
 * en memoria que simula cortes y escrituras parciales.
 */

/// Almacén de segmentos y metadatos. La implementación real es LittleFS.
class AlmacenSegmentos {
public:
    virtual ~AlmacenSegmentos() = default;

    virtual std::vector<uint32_t> listarSegmentos() = 0;
    virtual size_t tamanoSegmento(uint32_t id) = 0;
    /// Añade al final (crea el segmento si no existe). Devuelve los bytes
    /// escritos: menos que `longitud` = escritura parcial.
    virtual size_t anexar(uint32_t id, const uint8_t* datos, size_t longitud) = 0;
    virtual bool leer(uint32_t id, size_t desplazamiento, uint8_t* datos, size_t longitud) = 0;
    virtual bool borrarSegmento(uint32_t id) = 0;

    /// Metadatos pequeños (cursor). Reemplazo atómico del contenido.
    virtual bool leerMeta(const char* nombre, uint8_t* datos, size_t longitud) = 0;
    virtual bool escribirMeta(const char* nombre, const uint8_t* datos, size_t longitud) = 0;

    /// Cuarentena: archivo aparte de solo-anexar.
    virtual size_t tamanoCuarentena() = 0;
    virtual size_t anexarCuarentena(const uint8_t* datos, size_t longitud) = 0;

    /// Bytes libres en el sistema de archivos (margen de seguridad).
    virtual size_t bytesLibres() = 0;
};

/// Estado persistente de las muestras no almacenadas por saturación.
struct EstadoPerdidas {
    uint32_t total = 0;          ///< acumulado histórico (no se reinicia)
    uint32_t episodio = 0;       ///< muestras perdidas en el episodio vigente/último
    bool episodioAbierto = false;
    uint32_t desdeBoot = 0, desdeSeq = 0, desdeEpoch = 0;
    uint32_t hastaBoot = 0, hastaSeq = 0, hastaEpoch = 0;
};

/// Persistencia del contador de pérdidas. Va FUERA de LittleFS (NVS en el
/// ESP32): justo cuando hay que escribirlo, LittleFS está lleno.
class PersistenciaPerdidas {
public:
    virtual ~PersistenciaPerdidas() = default;
    virtual bool cargar(EstadoPerdidas& estado) = 0;
    virtual bool guardar(const EstadoPerdidas& estado) = 0;
};

struct ConfigCola {
    uint32_t registrosPorSegmento = 100;  ///< 100 × 40 B = 4000 B ≈ 1 bloque de 4 KB
    uint32_t maxSegmentos = 300;          ///< 30 000 lecturas ≈ 10,4 días a 30 s
    uint8_t umbralAvisoPct = 80;
    uint8_t umbralRearmePct = 70;
    uint32_t maxRegistrosCuarentena = 250;
    size_t reservaBytesLibres = 16 * 1024;  ///< margen para cursor, CA y COW de LittleFS
};

/// Bloques que se dejan para metadatos de directorio y copia en escritura de
/// LittleFS al dimensionar la partición (300 entradas de directorio ocupan
/// varios pares de metadatos).
constexpr size_t BLOQUES_METADATOS_LITTLEFS = 16;

/// Bytes mínimos de la partición LittleFS para la capacidad nominal de la
/// cola: un bloque por segmento (4000 B caben en uno de 4096), la reserva
/// libre que exige `guardar` y los metadatos. Con menos, la cola se satura
/// antes de llegar a `capacidadRegistros()`.
size_t bytesMinimosParticion(const ConfigCola& config, size_t tamBloque = 4096);

enum class ResultadoGuardado { Guardado, SaturadoNoAlmacenado, ErrorEscritura };
enum class EstadoCabeza { Vacia, Ok, Corrupta };

/// Motivo de cuarentena (primer byte de cada entrada de 44 B).
enum class MotivoCuarentena : uint8_t {
    CrcInvalido = 1,
    PayloadInvalido = 2,
    RechazadoBackend = 3,
};

struct InformeMontaje {
    uint32_t segmentos = 0;
    uint32_t pendientes = 0;
    uint32_t segmentosConColaParcial = 0;
    uint32_t bytesParcialesIgnorados = 0;
    uint32_t segmentosVaciosBorrados = 0;
    bool cursorReconstruido = false;
};

class ColaLecturas {
public:
    ColaLecturas(AlmacenSegmentos& almacen, PersistenciaPerdidas& perdidas, const ConfigCola& config);

    /// Reconstruye el índice desde el almacén. Llamar una vez al arrancar.
    InformeMontaje montar();

    /// Guarda una lectura al final de la cola.
    ResultadoGuardado guardar(const RegistroLectura& registro);

    /// Guarda `n` lecturas seguidas escribiendo cada tramo de un mismo segmento
    /// en UNA operación del almacén (una llamada a LittleFS por tramo, no por
    /// registro). Mismo resultado que `n` llamadas a `guardar`; se detiene en
    /// el primer fallo. `guardados` recibe cuántas quedaron en la cola.
    ResultadoGuardado guardarLote(const RegistroLectura* registros, size_t n, size_t& guardados);

    /// Lee el registro más antiguo sin acuse. `bruto` recibe los 40 bytes tal
    /// como están en flash (para cuarentena si está corrupto).
    EstadoCabeza leerCabeza(RegistroLectura& registro, uint8_t bruto[TAM_REGISTRO]);

    /// Acuse lógico recibido para la cabeza: avanza el cursor y, si el
    /// segmento quedó completo y consumido, lo borra.
    bool confirmarCabeza();

    /// Copia la cabeza a cuarentena con su motivo y avanza. Nunca borra un
    /// registro sin dejar copia (salvo que la cuarentena esté llena, lo que
    /// se cuenta y se reporta).
    bool cuarentenarCabeza(MotivoCuarentena motivo, const uint8_t bruto[TAM_REGISTRO]);

    /// ADR-18 (solo banco, `buffer descartar`). Sella el último segmento para
    /// que lo que se guarde después vaya a uno nuevo y quede fuera del
    /// descarte, y devuelve su id (0 con la cola vacía).
    uint32_t prepararDescarte();
    /// Borra como mucho `maxSegmentos` segmentos de la cabeza con id hasta
    /// `ultimoId` (el de `prepararDescarte`), por tandas para no retener el
    /// cerrojo. Devuelve los registros pendientes descartados en la llamada;
    /// `quedan` indica si falta algún segmento. La cuarentena no se toca.
    uint32_t descartarTanda(uint32_t ultimoId, uint32_t maxSegmentos, bool& quedan);

    // ── Estado para diagnóstico / HU-48 ─────────────────────────────────
    uint32_t pendientes() const;
    uint32_t segmentos() const { return (uint32_t)_segmentos.size(); }
    uint32_t capacidadRegistros() const;
    uint8_t ocupacionPct() const;
    const EstadoPerdidas& perdidas() const { return _perdidas; }
    uint32_t cuarentenados() const { return _cuarentenados; }
    uint32_t cuarentenaDesbordada() const { return _cuarentenaDesbordada; }
    uint32_t erroresEscritura() const { return _erroresEscritura; }

    /// true una vez al cruzar el umbral de aviso (80 %).
    bool consumirAvisoOcupacion(uint8_t& pct);
    /// true si el estado de pérdidas cambió desde la última consulta.
    bool consumirAvisoPerdidas(EstadoPerdidas& estado);

private:
    struct Segmento {
        uint32_t id;
        uint32_t registros;
        bool sellado;
    };

    bool _hayEspacioParaSegmentoNuevo();
    void _registrarPerdida(const RegistroLectura& r);
    void _cerrarEpisodioSiAbierto();
    bool _persistirCursor();
    void _evaluarUmbral();
    void _avanzarCursor();
    /// Borra los segmentos sellados cuyo contenido ya tiene acuse completo.
    void _normalizarCabeza();

    AlmacenSegmentos& _almacen;
    PersistenciaPerdidas& _persistencia;
    ConfigCola _config;

    std::vector<Segmento> _segmentos;  ///< ordenados por id; [0] = cabeza
    uint32_t _cursorIndice = 0;        ///< próximo registro sin acuse dentro de _segmentos[0]
    uint32_t _siguienteId = 1;

    EstadoPerdidas _perdidas;
    bool _avisoPerdidasPendiente = false;

    bool _avisoArmado = true;
    bool _avisoPendiente = false;
    uint8_t _pctAviso = 0;

    uint32_t _cuarentenados = 0;
    uint32_t _cuarentenaDesbordada = 0;
    uint32_t _erroresEscritura = 0;
};

// ---------------------------------------------------------------------------
// Drenaje hacia el backend
// ---------------------------------------------------------------------------

enum class ResultadoEnvio {
    Confirmado,   ///< acuse lógico committed/duplicate → liberar
    Rechazado,    ///< acuse lógico rejected → cuarentena
    Invalido,     ///< no se pudo serializar/validar → cuarentena, NO se publica
    Fallo,        ///< sin PUBACK o sin acuse lógico a tiempo → conservar y parar
    SinConexion,  ///< sin sesión → conservar y parar
};

/**
 * IOT-008: resultado de un acuse lógico. Un acuse que autoriza liberar pero
 * cuya huella no corresponde a ningún JSON publicado por el nodo se trata
 * como rechazo: el registro va a cuarentena y la copia local se conserva.
 */
ResultadoEnvio resultadoDeAck(EstadoAck estado, bool huellaCoincide);

class Transmisor {
public:
    virtual ~Transmisor() = default;
    virtual ResultadoEnvio enviar(const RegistroLectura& registro) = 0;
};

/// Exclusión mutua entre el núcleo de captura y el de red. `drenar()` la toma
/// SOLO alrededor de cada operación sobre la cola y la suelta durante
/// `enviar()`: si se mantuviera durante el PUBACK + acuse lógico (hasta ~13 s),
/// la tarea de sensores quedaría bloqueada y perdería su cadencia.
class Cerrojo {
public:
    virtual ~Cerrojo() = default;
    virtual void tomar() = 0;
    virtual void soltar() = 0;
};

struct ResumenDrenaje {
    int confirmados = 0;
    int rechazados = 0;
    int invalidos = 0;
    int corruptos = 0;
    bool detenido = false;
};

/**
 * Drena en orden FIFO estricto:
 *   1. Solo `Confirmado` (acuse lógico) libera un registro. El PUBACK no basta.
 *   2. Al primer `Fallo`/`SinConexion` se detiene: en el siguiente intento se
 *      reanuda desde el primer registro sin acuse (HU-07 esc. 2); si ya se
 *      había publicado, el backend lo deduplica (HU-11).
 *   3. Corrupto/Rechazado/Inválido → cuarentena y se sigue: uno malo no
 *      bloquea a los válidos que vienen detrás.
 *   4. Como mucho `maxPorCiclo` envíos, para ceder CPU y alimentar el watchdog.
 */
ResumenDrenaje drenar(ColaLecturas& cola, Transmisor& transmisor, int maxPorCiclo,
                      Cerrojo* cerrojo = nullptr);

}  // namespace core

#endif  // CORE_COLA_LECTURAS_H
