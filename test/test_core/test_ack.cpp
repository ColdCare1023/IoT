/**
 * Acuse lógico del backend (HU-07): solo un acuse explícito de persistencia
 * autoriza liberar un registro del buffer.
 */
#include <unity.h>

#include "core/Ack.h"
#include "core/ColaLecturas.h"
#include "core/Sha256.h"

using core::EstadoAck;

// IOT-008: todo acuse válido trae la huella de los bytes que recibió el backend.
static const char* HUELLA = "\"content_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"";

void test_ack_committed_formato_compacto(void) {
    core::AckLogico a;
    TEST_ASSERT_TRUE(core::parsearAck(
        std::string("{\"device_id\":\"ESP32_FARMACIA_01\",\"boot_id\":42,\"seq_no\":1205,\"status\":\"committed\",") +
            HUELLA + "}", a));
    TEST_ASSERT_EQUAL_STRING("ESP32_FARMACIA_01", a.deviceId.c_str());
    TEST_ASSERT_EQUAL_UINT32(42, a.bootId);
    TEST_ASSERT_EQUAL_UINT32(1205, a.seqNo);
    TEST_ASSERT_TRUE(a.estado == EstadoAck::Confirmado);
    TEST_ASSERT_TRUE(core::ackAutorizaLiberar(a.estado));
}

void test_ack_formato_python_y_alias_en_espanol(void) {
    core::AckLogico a;
    TEST_ASSERT_TRUE(core::parsearAck(
        std::string("{\"device_id\": \"N1\", \"boot_id\": 3, \"seq_no\": 9, \"estado\": \"duplicado\", ") +
            HUELLA + "}", a));
    TEST_ASSERT_TRUE(a.estado == EstadoAck::Duplicado);
    TEST_ASSERT_TRUE(core::ackAutorizaLiberar(a.estado));

    TEST_ASSERT_TRUE(core::parsearAck(
        std::string("{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"rejected\",\"reason\":\"schema\",") +
            HUELLA + "}", a));
    TEST_ASSERT_TRUE(a.estado == EstadoAck::Rechazado);
    TEST_ASSERT_FALSE(core::ackAutorizaLiberar(a.estado));
}

/// Un acuse ambiguo nunca autoriza un borrado.
void test_ack_incompleto_o_desconocido_se_ignora(void) {
    core::AckLogico a;
    TEST_ASSERT_FALSE(core::parsearAck("{\"device_id\":\"N1\",\"seq_no\":9,\"status\":\"committed\"}", a));
    TEST_ASSERT_FALSE(core::parsearAck("{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9}", a));
    TEST_ASSERT_FALSE(core::parsearAck("{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"ok\"}", a));
    TEST_ASSERT_FALSE(core::parsearAck("{\"device_id\":\"N1\",\"boot_id\":\"3\",\"seq_no\":9,\"status\":\"committed\"}", a));
    TEST_ASSERT_FALSE(core::parsearAck("PUBACK", a));
    TEST_ASSERT_FALSE(core::parsearAck("", a));
}

void test_memoria_de_acks(void) {
    core::MemoriaAcks m;
    EstadoAck e;
    std::string h;
    TEST_ASSERT_FALSE(m.buscar(1, 1, e, h));
    m.anotar(1, 1, EstadoAck::Confirmado, "h1");
    m.anotar(1, 2, EstadoAck::Rechazado, "h2");
    TEST_ASSERT_TRUE(m.buscar(1, 1, e, h));
    TEST_ASSERT_TRUE(e == EstadoAck::Confirmado);
    TEST_ASSERT_EQUAL_STRING("h1", h.c_str());
    TEST_ASSERT_TRUE(m.buscar(1, 2, e, h));
    TEST_ASSERT_TRUE(e == EstadoAck::Rechazado);
    TEST_ASSERT_FALSE(m.buscar(2, 1, e, h));  // otro boot_id, mismo seq_no
    m.olvidar(1, 1);
    TEST_ASSERT_FALSE(m.buscar(1, 1, e, h));

    // Capacidad circular: las entradas más viejas se reemplazan.
    for (uint32_t i = 100; i < 100 + core::MemoriaAcks::CAPACIDAD + 5; ++i) {
        m.anotar(9, i, EstadoAck::Confirmado, "h");
    }
    TEST_ASSERT_FALSE(m.buscar(9, 100, e, h));
    TEST_ASSERT_TRUE(m.buscar(9, 100 + core::MemoriaAcks::CAPACIDAD + 4, e, h));
}

/// IOT-008: un acuse sin huella, o con una huella mal formada, no se acepta.
void test_ack_sin_huella_valida_se_ignora(void) {
    core::AckLogico a;
    TEST_ASSERT_FALSE(core::parsearAck("{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"committed\"}", a));
    TEST_ASSERT_FALSE(core::parsearAck(
        "{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"committed\",\"content_sha256\":\"abc\"}", a));
    TEST_ASSERT_FALSE(core::parsearAck(std::string("{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"committed\","
                                                   "\"content_sha256\":\"") + std::string(64, 'G') + "\"}", a));
}

void test_memoria_de_envios_acepta_cualquier_version_enviada(void) {
    core::MemoriaEnvios envios;
    const std::string v1 = core::sha256Hex("{\"time_quality\":\"unsynced\"}");
    const std::string v2 = core::sha256Hex("{\"time_quality\":\"sntp_backfilled\"}");
    envios.anotar(42, 7, v1);
    envios.anotar(42, 7, v2);  // reenvío con otra base de tiempo
    TEST_ASSERT_TRUE(envios.coincide(42, 7, v1));
    TEST_ASSERT_TRUE(envios.coincide(42, 7, v2));
    TEST_ASSERT_FALSE(envios.coincide(42, 8, v1));                   // otro registro
    TEST_ASSERT_FALSE(envios.coincide(42, 7, core::sha256Hex("x")));  // contenido ajeno
}

void test_solo_un_acuse_con_la_huella_de_lo_enviado_libera(void) {
    using core::ResultadoEnvio;
    TEST_ASSERT_TRUE(core::resultadoDeAck(EstadoAck::Confirmado, true) == ResultadoEnvio::Confirmado);
    TEST_ASSERT_TRUE(core::resultadoDeAck(EstadoAck::Duplicado, true) == ResultadoEnvio::Confirmado);
    // Huella ajena: NO se borra, va a cuarentena.
    TEST_ASSERT_TRUE(core::resultadoDeAck(EstadoAck::Confirmado, false) == ResultadoEnvio::Rechazado);
    TEST_ASSERT_TRUE(core::resultadoDeAck(EstadoAck::Duplicado, false) == ResultadoEnvio::Rechazado);
    // Conflicto (BE-001): rejected → cuarentena, coincida o no.
    TEST_ASSERT_TRUE(core::resultadoDeAck(EstadoAck::Rechazado, true) == ResultadoEnvio::Rechazado);
}

void run_tests_ack(void) {
    RUN_TEST(test_ack_sin_huella_valida_se_ignora);
    RUN_TEST(test_memoria_de_envios_acepta_cualquier_version_enviada);
    RUN_TEST(test_solo_un_acuse_con_la_huella_de_lo_enviado_libera);
    RUN_TEST(test_ack_committed_formato_compacto);
    RUN_TEST(test_ack_formato_python_y_alias_en_espanol);
    RUN_TEST(test_ack_incompleto_o_desconocido_se_ignora);
    RUN_TEST(test_memoria_de_acks);
}
