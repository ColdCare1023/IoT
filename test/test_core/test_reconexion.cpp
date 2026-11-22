/**
 * HU-08 esc. 3 / H1: el episodio de reconexión (intentos, desde cuándo,
 * reinicios) sobrevive a un reinicio por watchdog y se reporta al volver.
 */
#include <unity.h>

#include <string>
#include <vector>

#include "core/Json.h"
#include "core/Reconexion.h"

using core::EpisodioReconexion;
using core::EtapaReconexion;
using core::MarcaTiempo;

static const uint32_t EPOCH_RC = 1790350200u;  // 2026-09-25T15:30:00Z

/// NVS en memoria: los bytes persisten entre "reinicios" (instancias nuevas
/// del seguimiento sobre el mismo almacén).
class NvsMemoria : public core::PersistenciaReconexion {
public:
    std::vector<uint8_t> datos;
    int escrituras = 0;
    bool leer(uint8_t* d, size_t n) override {
        if (datos.size() != n) return false;
        for (size_t i = 0; i < n; ++i) d[i] = datos[i];
        return true;
    }
    bool escribir(const uint8_t* d, size_t n) override {
        datos.assign(d, d + n);
        escrituras++;
        return true;
    }
};

static MarcaTiempo marca(uint32_t boot, uint32_t uptime, uint32_t epoch = 0) {
    MarcaTiempo m;
    m.bootId = boot;
    m.uptimeS = uptime;
    m.epoch = epoch;
    return m;
}

static core::BaseTiempo baseDe(uint32_t boot, uint32_t epoch, uint32_t uptime) {
    core::BaseTiempo b;
    b.fijar(boot, epoch, uptime);
    return b;
}

void test_reconexion_codigo_ida_y_vuelta_y_crc(void) {
    EpisodioReconexion e;
    e.activo = true;
    e.diagnostico = true;
    e.etapa = EtapaReconexion::Broker;
    e.intentos = 1234;
    e.reinicios = 7;
    e.desde = marca(41, 3600, EPOCH_RC);
    e.hasta = marca(48, 120, 0);
    uint8_t buf[core::TAM_EPISODIO_RECONEXION];
    core::codificarEpisodio(e, buf);

    EpisodioReconexion r;
    TEST_ASSERT_TRUE(core::decodificarEpisodio(buf, r));
    TEST_ASSERT_TRUE(r.activo);
    TEST_ASSERT_TRUE(r.diagnostico);
    TEST_ASSERT_TRUE(r.etapa == EtapaReconexion::Broker);
    TEST_ASSERT_EQUAL_UINT32(1234, r.intentos);
    TEST_ASSERT_EQUAL_UINT32(7, r.reinicios);
    TEST_ASSERT_EQUAL_UINT32(41, r.desde.bootId);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_RC, r.desde.epoch);
    TEST_ASSERT_EQUAL_UINT32(120, r.hasta.uptimeS);

    buf[9] ^= 0x01;  // un bit cambiado en los intentos
    TEST_ASSERT_FALSE(core::decodificarEpisodio(buf, r));
    core::codificarEpisodio(e, buf);
    buf[0] = 'X';
    TEST_ASSERT_FALSE(core::decodificarEpisodio(buf, r));
}

void test_reconexion_episodio_empieza_al_primer_fallo_y_cruza_el_umbral_una_vez(void) {
    NvsMemoria nvs;
    core::SeguimientoReconexion s(nvs, 3, 60);
    s.montar(10);
    TEST_ASSERT_FALSE(s.episodio().activo);

    TEST_ASSERT_FALSE(s.registrarFallo(EtapaReconexion::WiFi, marca(10, 100, EPOCH_RC)));
    TEST_ASSERT_TRUE(s.episodio().activo);
    TEST_ASSERT_EQUAL_UINT32(1, s.episodio().intentos);
    TEST_ASSERT_EQUAL_UINT32(100, s.episodio().desde.uptimeS);
    TEST_ASSERT_EQUAL_INT(1, nvs.escrituras);  // se guarda al abrir

    TEST_ASSERT_FALSE(s.registrarFallo(EtapaReconexion::WiFi, marca(10, 102)));
    TEST_ASSERT_TRUE(s.registrarFallo(EtapaReconexion::Broker, marca(10, 106)));  // 3.º: cruza
    TEST_ASSERT_TRUE(s.episodio().diagnostico);
    TEST_ASSERT_TRUE(s.episodio().etapa == EtapaReconexion::Broker);
    TEST_ASSERT_EQUAL_INT(2, nvs.escrituras);  // y al cruzar el umbral
    TEST_ASSERT_FALSE(s.registrarFallo(EtapaReconexion::Broker, marca(10, 120)));  // no vuelve a cruzar
    TEST_ASSERT_EQUAL_UINT32(4, s.episodio().intentos);
    // El inicio no se mueve con los fallos siguientes.
    TEST_ASSERT_EQUAL_UINT32(EPOCH_RC, s.episodio().desde.epoch);
}

/// H1: el nodo reinicia por TASK_WDT con el episodio abierto. El seguimiento
/// nuevo (otro arranque) lo recupera de NVS, cuenta el reinicio y sigue
/// sumando intentos desde donde iba, con la misma hora de inicio.
void test_reconexion_el_episodio_sobrevive_a_un_reinicio_por_watchdog(void) {
    NvsMemoria nvs;
    {
        core::SeguimientoReconexion s(nvs, 5, 60);
        s.montar(20);
        for (uint32_t i = 0; i < 6; ++i) s.registrarFallo(EtapaReconexion::Broker, marca(20, 1000 + i * 70, EPOCH_RC + i * 70));
        TEST_ASSERT_TRUE(s.episodio().diagnostico);
    }
    core::SeguimientoReconexion tras(nvs, 5, 60);
    tras.montar(21);
    const EpisodioReconexion& e = tras.episodio();
    TEST_ASSERT_TRUE(e.activo);
    TEST_ASSERT_TRUE(e.diagnostico);
    TEST_ASSERT_EQUAL_UINT32(1, e.reinicios);
    TEST_ASSERT_EQUAL_UINT32(6, e.intentos);
    TEST_ASSERT_EQUAL_UINT32(20, e.desde.bootId);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_RC, e.desde.epoch);

    tras.registrarFallo(EtapaReconexion::Broker, marca(21, 70));  // ≥ 60 s tras el último guardado
    TEST_ASSERT_EQUAL_UINT32(7, tras.episodio().intentos);

    // Un segundo reinicio también se cuenta.
    core::SeguimientoReconexion otra(nvs, 5, 60);
    otra.montar(22);
    TEST_ASSERT_EQUAL_UINT32(2, otra.episodio().reinicios);
    TEST_ASSERT_EQUAL_UINT32(7, otra.episodio().intentos);
}

/// Entre el inicio y el umbral se guarda como mucho una vez por intervalo: un
/// reintento cada pocos segundos no escribe la flash cada vez, y un reinicio
/// pierde a lo sumo los intentos de ese intervalo, nunca el inicio.
void test_reconexion_persistencia_acotada_por_intervalo(void) {
    NvsMemoria nvs;
    core::SeguimientoReconexion s(nvs, 1000, 60);
    s.montar(5);
    s.registrarFallo(EtapaReconexion::WiFi, marca(5, 100));  // abre: escribe
    for (uint32_t t = 105; t < 160; t += 5) s.registrarFallo(EtapaReconexion::WiFi, marca(5, t));
    TEST_ASSERT_EQUAL_INT(1, nvs.escrituras);
    s.registrarFallo(EtapaReconexion::WiFi, marca(5, 160));  // 60 s después del último guardado
    TEST_ASSERT_EQUAL_INT(2, nvs.escrituras);
    const uint32_t guardados = s.episodio().intentos;
    s.registrarFallo(EtapaReconexion::WiFi, marca(5, 170));  // sin guardar

    core::SeguimientoReconexion tras(nvs, 1000, 60);
    tras.montar(6);
    TEST_ASSERT_EQUAL_UINT32(guardados, tras.episodio().intentos);
    TEST_ASSERT_EQUAL_UINT32(100, tras.episodio().desde.uptimeS);
}

void test_reconexion_al_conectar_se_cierra_y_se_reporta_solo_si_hizo_falta(void) {
    NvsMemoria nvs;
    core::SeguimientoReconexion s(nvs, 5, 60);
    s.montar(3);
    EpisodioReconexion cerrado;
    TEST_ASSERT_FALSE(s.registrarConexion(marca(3, 10), cerrado));  // sin episodio

    // Corte breve: 1 fallo y vuelve. Se cierra, queda como último episodio,
    // pero no amerita el evento de diagnóstico.
    s.registrarFallo(EtapaReconexion::WiFi, marca(3, 50));
    TEST_ASSERT_FALSE(s.registrarConexion(marca(3, 65), cerrado));
    TEST_ASSERT_FALSE(s.episodio().activo);
    TEST_ASSERT_EQUAL_UINT32(1, s.episodio().intentos);

    // Episodio largo: se reporta.
    for (uint32_t i = 0; i < 5; ++i) s.registrarFallo(EtapaReconexion::WiFi, marca(3, 100 + i * 60));
    TEST_ASSERT_TRUE(s.registrarConexion(marca(3, 900), cerrado));
    TEST_ASSERT_EQUAL_UINT32(5, cerrado.intentos);
    TEST_ASSERT_EQUAL_UINT32(100, cerrado.desde.uptimeS);
    TEST_ASSERT_EQUAL_UINT32(900, cerrado.hasta.uptimeS);

    // Cerrado y guardado: el siguiente arranque no lo reabre ni cuenta reinicio.
    core::SeguimientoReconexion tras(nvs, 5, 60);
    tras.montar(4);
    TEST_ASSERT_FALSE(tras.episodio().activo);
    TEST_ASSERT_EQUAL_UINT32(0, tras.episodio().reinicios);
    TEST_ASSERT_EQUAL_UINT32(5, tras.episodio().intentos);
}

/// Un reinicio en medio basta para reportar el episodio, aunque no llegara al
/// umbral (un bucle de reinicios es justo lo que hay que ver).
void test_reconexion_con_reinicio_en_medio_se_reporta_aunque_no_llegue_al_umbral(void) {
    NvsMemoria nvs;
    {
        core::SeguimientoReconexion s(nvs, 5, 60);
        s.montar(7);
        s.registrarFallo(EtapaReconexion::Broker, marca(7, 40));
    }
    core::SeguimientoReconexion tras(nvs, 5, 60);
    tras.montar(8);
    EpisodioReconexion cerrado;
    TEST_ASSERT_TRUE(tras.registrarConexion(marca(8, 25), cerrado));
    TEST_ASSERT_EQUAL_UINT32(1, cerrado.reinicios);
}

void test_reconexion_el_inicio_es_la_perdida_de_sesion(void) {
    NvsMemoria nvs;
    core::SeguimientoReconexion s(nvs, 5, 60);
    s.montar(9);
    s.marcarDesconexion(marca(9, 500, EPOCH_RC));
    s.registrarFallo(EtapaReconexion::WiFi, marca(9, 515, EPOCH_RC + 15));
    TEST_ASSERT_EQUAL_UINT32(500, s.episodio().desde.uptimeS);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_RC, s.episodio().desde.epoch);
}

void test_reconexion_nvs_ilegible_empieza_sin_episodio(void) {
    NvsMemoria nvs;
    nvs.datos.assign(core::TAM_EPISODIO_RECONEXION, 0xAB);
    core::SeguimientoReconexion s(nvs, 5, 60);
    s.montar(2);
    TEST_ASSERT_FALSE(s.episodio().activo);
    TEST_ASSERT_EQUAL_UINT32(0, s.episodio().intentos);
}

void test_reconexion_detalle_del_evento_con_y_sin_hora(void) {
    EpisodioReconexion e;
    e.diagnostico = true;
    e.etapa = EtapaReconexion::WiFi;
    e.intentos = 9;
    e.reinicios = 0;
    e.desde = marca(30, 100, EPOCH_RC);
    e.hasta = marca(30, 7300, EPOCH_RC + 7200);
    const core::BaseTiempo base = baseDe(30, EPOCH_RC - 100, 0);
    const std::string j = core::detalleReconexionProlongada(e, 5, base);
    std::string v;
    uint32_t n = 0;
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "intentos_fallidos", n));
    TEST_ASSERT_EQUAL_UINT32(9, n);
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "duracion_s", n));
    TEST_ASSERT_EQUAL_UINT32(7200, n);
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "umbral_intentos", n));
    TEST_ASSERT_EQUAL_UINT32(5, n);
    TEST_ASSERT_TRUE(core::leerCadena(j, "desde", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "hasta", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T17:30:00Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "etapa", v));
    TEST_ASSERT_EQUAL_STRING("wifi", v.c_str());

    // Empezó en un arranque sin SNTP y terminó en otro: no se inventa nada.
    e.desde = marca(31, 100, 0);
    e.hasta = marca(33, 50, 0);
    e.reinicios = 2;
    const std::string k = core::detalleReconexionProlongada(e, 5, baseDe(33, EPOCH_RC, 40));
    TEST_ASSERT_TRUE(core::buscarCampo(k, "desde", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::buscarCampo(k, "duracion_s", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::leerCadena(k, "hasta", v));  // retrofechado en su propio arranque
    TEST_ASSERT_TRUE(core::leerSinSigno32(k, "reinicios", n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

void run_tests_reconexion(void) {
    RUN_TEST(test_reconexion_codigo_ida_y_vuelta_y_crc);
    RUN_TEST(test_reconexion_episodio_empieza_al_primer_fallo_y_cruza_el_umbral_una_vez);
    RUN_TEST(test_reconexion_el_episodio_sobrevive_a_un_reinicio_por_watchdog);
    RUN_TEST(test_reconexion_persistencia_acotada_por_intervalo);
    RUN_TEST(test_reconexion_al_conectar_se_cierra_y_se_reporta_solo_si_hizo_falta);
    RUN_TEST(test_reconexion_con_reinicio_en_medio_se_reporta_aunque_no_llegue_al_umbral);
    RUN_TEST(test_reconexion_el_inicio_es_la_perdida_de_sesion);
    RUN_TEST(test_reconexion_nvs_ilegible_empieza_sin_episodio);
    RUN_TEST(test_reconexion_detalle_del_evento_con_y_sin_hora);
}
