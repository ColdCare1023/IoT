/**
 * ADR-18: llenado rápido del buffer para el ensayo de saturación.
 */
#include <unity.h>

#include "core/LlenadoBanco.h"

void test_la_tanda_respeta_el_maximo_y_lo_pedido(void) {
    TEST_ASSERT_EQUAL_UINT32(20, core::tandaLlenadoBanco(30000, 0, 30000, 20));
    TEST_ASSERT_EQUAL_UINT32(7, core::tandaLlenadoBanco(7, 0, 30000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, core::tandaLlenadoBanco(0, 0, 30000, 20));
}

void test_la_tanda_no_pasa_de_la_capacidad(void) {
    TEST_ASSERT_EQUAL_UINT32(3, core::tandaLlenadoBanco(100, 29997, 30000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, core::tandaLlenadoBanco(100, 30000, 30000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, core::tandaLlenadoBanco(100, 30001, 30000, 20));
}

void test_el_registro_sintetico_va_marcado_y_en_rango(void) {
    core::RegistroLectura real;
    real.bootId = 42;
    real.seqNo = 10;
    real.tempDsCenti = 2350;  // ambiente: fuera de 2-8 °C
    real.falloDs = core::FalloSensor::SinRespuesta;
    real.poner(core::BANDERA_MC38_INSTALADO, true);

    const core::RegistroLectura r = core::registroSintetico(real, 11, 900, 1793131200);

    TEST_ASSERT_EQUAL_UINT32(42, r.bootId);
    TEST_ASSERT_EQUAL_UINT32(11, r.seqNo);
    TEST_ASSERT_EQUAL_UINT32(900, r.uptimeS);
    TEST_ASSERT_TRUE(r.tiene(core::BANDERA_SIMULADO));
    TEST_ASSERT_TRUE(r.tiene(core::BANDERA_TIEMPO_SNTP));
    TEST_ASSERT_TRUE(r.tiene(core::BANDERA_MC38_INSTALADO));
    TEST_ASSERT_EQUAL_INT16(core::TEMP_SINTETICA_CENTI, r.tempDsCenti);
    TEST_ASSERT_TRUE(r.falloDs == core::FalloSensor::Ninguno);
}

void test_sin_sntp_el_sintetico_no_inventa_la_hora(void) {
    core::RegistroLectura real;
    real.poner(core::BANDERA_TIEMPO_SNTP, true);
    const core::RegistroLectura r = core::registroSintetico(real, 1, 5, 0);
    TEST_ASSERT_FALSE(r.tiene(core::BANDERA_TIEMPO_SNTP));
    TEST_ASSERT_EQUAL_UINT32(0, r.capturadoEpoch);
}

void run_tests_llenado_banco(void) {
    RUN_TEST(test_la_tanda_respeta_el_maximo_y_lo_pedido);
    RUN_TEST(test_la_tanda_no_pasa_de_la_capacidad);
    RUN_TEST(test_el_registro_sintetico_va_marcado_y_en_rango);
    RUN_TEST(test_sin_sntp_el_sintetico_no_inventa_la_hora);
}
