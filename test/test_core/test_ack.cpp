/**
 * Acuse lógico del backend (HU-07): solo un acuse explícito de persistencia
 * autoriza liberar un registro del buffer.
 */
#include <unity.h>

#include "core/Ack.h"

using core::EstadoAck;

void test_ack_committed_formato_compacto(void) {
    core::AckLogico a;
    TEST_ASSERT_TRUE(core::parsearAck(
        "{\"device_id\":\"ESP32_FARMACIA_01\",\"boot_id\":42,\"seq_no\":1205,\"status\":\"committed\"}", a));
    TEST_ASSERT_EQUAL_STRING("ESP32_FARMACIA_01", a.deviceId.c_str());
    TEST_ASSERT_EQUAL_UINT32(42, a.bootId);
    TEST_ASSERT_EQUAL_UINT32(1205, a.seqNo);
    TEST_ASSERT_TRUE(a.estado == EstadoAck::Confirmado);
    TEST_ASSERT_TRUE(core::ackAutorizaLiberar(a.estado));
}

void test_ack_formato_python_y_alias_en_espanol(void) {
    core::AckLogico a;
    TEST_ASSERT_TRUE(core::parsearAck(
        "{\"device_id\": \"N1\", \"boot_id\": 3, \"seq_no\": 9, \"estado\": \"duplicado\"}", a));
    TEST_ASSERT_TRUE(a.estado == EstadoAck::Duplicado);
    TEST_ASSERT_TRUE(core::ackAutorizaLiberar(a.estado));

    TEST_ASSERT_TRUE(core::parsearAck(
        "{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"rejected\",\"reason\":\"schema\"}", a));
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
    TEST_ASSERT_FALSE(m.buscar(1, 1, e));
    m.anotar(1, 1, EstadoAck::Confirmado);
    m.anotar(1, 2, EstadoAck::Rechazado);
    TEST_ASSERT_TRUE(m.buscar(1, 1, e));
    TEST_ASSERT_TRUE(e == EstadoAck::Confirmado);
    TEST_ASSERT_TRUE(m.buscar(1, 2, e));
    TEST_ASSERT_TRUE(e == EstadoAck::Rechazado);
    TEST_ASSERT_FALSE(m.buscar(2, 1, e));  // otro boot_id, mismo seq_no
    m.olvidar(1, 1);
    TEST_ASSERT_FALSE(m.buscar(1, 1, e));

    // Capacidad circular: las entradas más viejas se reemplazan.
    for (uint32_t i = 100; i < 100 + core::MemoriaAcks::CAPACIDAD + 5; ++i) {
        m.anotar(9, i, EstadoAck::Confirmado);
    }
    TEST_ASSERT_FALSE(m.buscar(9, 100, e));
    TEST_ASSERT_TRUE(m.buscar(9, 100 + core::MemoriaAcks::CAPACIDAD + 4, e));
}

void run_tests_ack(void) {
    RUN_TEST(test_ack_committed_formato_compacto);
    RUN_TEST(test_ack_formato_python_y_alias_en_espanol);
    RUN_TEST(test_ack_incompleto_o_desconocido_se_ignora);
    RUN_TEST(test_memoria_de_acks);
}
