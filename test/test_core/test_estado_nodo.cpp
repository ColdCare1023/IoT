/**
 * `estado_nodo` (HU-48 esc. 1, HU-08 esc. 3, HU-04, HU-02): campos, rango
 * real de los pendientes y peor caso dentro de los límites del backend y del
 * buffer MQTT del nodo. Y el `desconexion_ordenada` de HU-13 esc. 2.
 */
#include <unity.h>

#include <climits>
#include <string>

#include "config.h"
#include "core/EstadoNodo.h"
#include "core/Json.h"
#include "core/Payload.h"

static const uint32_t EPOCH_EN = 1790350200u;  // 2026-09-25T15:30:00Z

static core::RegistroLectura registroEn(uint32_t boot, uint32_t seq, uint32_t uptime, uint32_t epoch) {
    core::RegistroLectura r;
    r.bootId = boot;
    r.seqNo = seq;
    r.uptimeS = uptime;
    if (epoch != 0) {
        r.capturadoEpoch = epoch;
        r.poner(core::BANDERA_TIEMPO_SNTP, true);
    }
    return r;
}

static size_t clavesPrimerNivel(const std::string& json) {
    // Objeto plano sin comas dentro de las cadenas (los valores son números,
    // booleanos, null, fechas ISO y nombres de etapa).
    if (json.size() <= 2) return 0;
    size_t n = 1;
    for (char c : json) n += c == ',' ? 1 : 0;
    return n;
}

void test_estado_nodo_conserva_los_campos_anteriores(void) {
    core::DatosEstadoNodo d;
    d.pendientes = 3;
    d.capacidad = 30000;
    d.rssiDbm = -67;
    d.relojSntp = true;
    const std::string j = core::detalleEstadoNodo(d, core::BaseTiempo());
    const char* anteriores[] = {"pendientes", "ocupacion_pct", "capacidad", "fs_total_bytes", "fs_libres_bytes",
                                "perdidas_total", "cuarentena", "cuarentena_desbordada", "ultimo_seq_no",
                                "rssi_dbm", "heap_libre", "heap_minimo", "intervalo_s", "pubacks_sin_ack",
                                "acks_huella_ajena", "eventos_descartados", "reloj_sntp",
                                "saltos_reloj_rechazados"};
    std::string v;
    for (const char* clave : anteriores) {
        TEST_ASSERT_TRUE_MESSAGE(core::buscarCampo(j, clave, v) != core::TipoValorJSON::NoEncontrado, clave);
    }
    // El JSON empieza igual que antes: los campos nuevos solo se agregan al final.
    TEST_ASSERT_EQUAL_INT(0, (int)j.find("{\"pendientes\":3,\"ocupacion_pct\":0,\"capacidad\":30000,"));
    TEST_ASSERT_TRUE(core::buscarCampo(j, "rssi_dbm", v) == core::TipoValorJSON::Numero);
    TEST_ASSERT_EQUAL_STRING("-67", v.c_str());
}

/// HU-48 esc. 1: el nodo informa el rango real de lo que retiene.
void test_estado_nodo_informa_el_rango_real_de_los_pendientes(void) {
    core::DatosEstadoNodo d;
    d.pendientes = 120;
    d.hayPrimerPendiente = true;
    d.primerPendiente = registroEn(42, 1, 30, EPOCH_EN);
    d.hayUltimoPendiente = true;
    d.ultimoPendiente = registroEn(42, 120, 3600, EPOCH_EN + 3570);
    const std::string j = core::detalleEstadoNodo(d, core::BaseTiempo());
    std::string v;
    TEST_ASSERT_TRUE(core::leerCadena(j, "pendientes_desde", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "pendientes_hasta", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T16:29:30Z", v.c_str());
}

void test_estado_nodo_sin_pendientes_el_rango_es_null(void) {
    core::DatosEstadoNodo d;
    d.pendientes = 0;
    d.hayPrimerPendiente = true;  // aunque se hubiera leído algo
    d.primerPendiente = registroEn(1, 1, 1, EPOCH_EN);
    const std::string j = core::detalleEstadoNodo(d, core::BaseTiempo());
    std::string v;
    TEST_ASSERT_TRUE(core::buscarCampo(j, "pendientes_desde", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::buscarCampo(j, "pendientes_hasta", v) == core::TipoValorJSON::Nulo);
}

/// Misma regla que `captured_at`: sin SNTP en su arranque no se inventa la
/// hora (null; el backend estima ese extremo); en el mismo arranque ya
/// sincronizado se retrofecha con el uptime.
void test_estado_nodo_rango_sin_hora_honesta_es_null(void) {
    core::BaseTiempo base;
    base.fijar(43, EPOCH_EN + 10000, 500);  // este arranque ya tiene SNTP
    core::DatosEstadoNodo d;
    d.pendientes = 50;
    d.hayPrimerPendiente = true;
    d.primerPendiente = registroEn(42, 7, 100, 0);  // arranque anterior sin SNTP
    d.hayUltimoPendiente = true;
    d.ultimoPendiente = registroEn(43, 60, 400, 0);  // este arranque, antes del SNTP
    std::string j = core::detalleEstadoNodo(d, base);
    std::string v;
    TEST_ASSERT_TRUE(core::buscarCampo(j, "pendientes_desde", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::leerCadena(j, "pendientes_hasta", v));
    TEST_ASSERT_EQUAL_STRING(core::formatearISO8601(EPOCH_EN + 10000 - 100).c_str(), v.c_str());

    // Un extremo ilegible (CRC) también sale null.
    d.hayUltimoPendiente = false;
    j = core::detalleEstadoNodo(d, base);
    TEST_ASSERT_TRUE(core::buscarCampo(j, "pendientes_hasta", v) == core::TipoValorJSON::Nulo);
}

/// HU-08 esc. 3: el episodio de reconexión (persistente) viaja en el latido.
void test_estado_nodo_incluye_el_episodio_de_reconexion(void) {
    core::DatosEstadoNodo d;
    d.reconexion.intentos = 37;
    d.reconexion.reinicios = 2;
    d.reconexion.diagnostico = true;
    d.reconexion.etapa = core::EtapaReconexion::Broker;
    d.reconexion.desde.bootId = 40;
    d.reconexion.desde.epoch = EPOCH_EN;
    d.reconexion.hasta.bootId = 42;
    d.reconexion.hasta.epoch = EPOCH_EN + 7300;
    const std::string j = core::detalleEstadoNodo(d, core::BaseTiempo());
    std::string v;
    uint32_t n = 0;
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "reconexion_intentos", n));
    TEST_ASSERT_EQUAL_UINT32(37, n);
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "reconexion_reinicios", n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_TRUE(core::leerCadena(j, "reconexion_desde", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "reconexion_hasta", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T17:31:40Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "reconexion_etapa", v));
    TEST_ASSERT_EQUAL_STRING("broker", v.c_str());

    // Sin episodio nunca: 0 intentos y el resto null.
    const std::string k = core::detalleEstadoNodo(core::DatosEstadoNodo(), core::BaseTiempo());
    TEST_ASSERT_TRUE(core::leerSinSigno32(k, "reconexion_intentos", n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_TRUE(core::buscarCampo(k, "reconexion_desde", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::buscarCampo(k, "reconexion_etapa", v) == core::TipoValorJSON::Nulo);
}

void test_estado_nodo_incluye_puerta_y_sht31(void) {
    core::DatosEstadoNodo d;
    d.puertaPendientes = 4;
    d.puertaDescartados = 1;
    d.sht31Recuperaciones = 2;
    const std::string j = core::detalleEstadoNodo(d, core::BaseTiempo());
    uint32_t n = 0;
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "puerta_pendientes", n));
    TEST_ASSERT_EQUAL_UINT32(4, n);
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "puerta_descartados", n));
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "sht31_recuperaciones", n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

/// Peor caso: dentro de los límites del backend (32 claves, 1024 B de
/// `detail`) y, con el sobre del evento, dentro del buffer MQTT del nodo.
void test_estado_nodo_peor_caso_cabe_en_backend_y_en_mqtt(void) {
    core::DatosEstadoNodo d;
    const uint32_t M = UINT32_MAX;
    d.pendientes = d.ocupacionPct = d.capacidad = d.fsTotalBytes = d.fsLibresBytes = M;
    d.perdidasTotal = d.cuarentena = d.cuarentenaDesbordada = d.ultimoSeqNo = M;
    d.rssiDbm = INT32_MIN;
    d.heapLibre = d.heapMinimo = d.intervaloS = d.pubacksSinAck = d.acksHuellaAjena = M;
    d.eventosDescartados = d.saltosRelojRechazados = M;
    d.relojSntp = false;
    d.hayPrimerPendiente = d.hayUltimoPendiente = true;
    d.primerPendiente = registroEn(M, M, M, 4102444799u);
    d.ultimoPendiente = registroEn(M, M, M, 4102444799u);
    d.reconexion.intentos = d.reconexion.reinicios = M;
    d.reconexion.etapa = core::EtapaReconexion::Broker;
    d.reconexion.desde.epoch = d.reconexion.hasta.epoch = 4102444799u;
    d.puertaPendientes = d.puertaDescartados = d.sht31Recuperaciones = M;
    const std::string detalle = core::detalleEstadoNodo(d, core::BaseTiempo());

    TEST_ASSERT_TRUE(clavesPrimerNivel(detalle) <= core::MAX_CLAVES_DETALLE_BACKEND);
    TEST_ASSERT_EQUAL_UINT32(28, (uint32_t)clavesPrimerNivel(detalle));
    TEST_ASSERT_TRUE(detalle.size() <= core::MAX_BYTES_DETALLE_BACKEND);

    const std::string device(50, 'A');  // device_id de longitud máxima
    core::BaseTiempo base;
    base.fijar(M, 4102444799u - 100, 100);
    const std::string evento = core::serializarEvento(device, M, "estado_nodo", 4102444799u, M, base, detalle);
    const size_t topico = std::string(TOPICO_PREFIJO).size() + device.size() + std::string("/eventos").size();
    // PUBLISH QoS 1: cabecera fija (1) + longitud restante (2) + longitud del
    // tópico (2) + tópico + id de paquete (2) + carga.
    const size_t paquete = 1 + 2 + 2 + topico + 2 + evento.size();
    TEST_ASSERT_TRUE_MESSAGE(paquete <= MQTT_BUFFER_BYTES, "estado_nodo no cabe en el buffer MQTT del nodo");
}

void test_desconexion_ordenada_lleva_el_motivo(void) {
    std::string v;
    const std::string j = core::detalleDesconexionOrdenada("rotacion_credencial");
    TEST_ASSERT_TRUE(core::leerCadena(j, "motivo", v));
    TEST_ASSERT_EQUAL_STRING("rotacion_credencial", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(core::detalleDesconexionOrdenada(nullptr), "motivo", v));
    TEST_ASSERT_EQUAL_STRING("desconocido", v.c_str());
}

void run_tests_estado_nodo(void) {
    RUN_TEST(test_estado_nodo_conserva_los_campos_anteriores);
    RUN_TEST(test_estado_nodo_informa_el_rango_real_de_los_pendientes);
    RUN_TEST(test_estado_nodo_sin_pendientes_el_rango_es_null);
    RUN_TEST(test_estado_nodo_rango_sin_hora_honesta_es_null);
    RUN_TEST(test_estado_nodo_incluye_el_episodio_de_reconexion);
    RUN_TEST(test_estado_nodo_incluye_puerta_y_sht31);
    RUN_TEST(test_estado_nodo_peor_caso_cabe_en_backend_y_en_mqtt);
    RUN_TEST(test_desconexion_ordenada_lleva_el_motivo);
}
