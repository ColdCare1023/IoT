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

/// `buffer cancelar` a mitad de una tanda deja el contador en 0 aunque la
/// tarea de captura siga descontando hasta terminarla.
void test_cancelar_a_mitad_de_una_tanda_deja_el_contador_en_cero(void) {
    core::BuzonLlenadoBanco buzon;
    uint32_t porLlenar = 0;
    buzon.pedirLlenado(30000);
    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Llenar);
    TEST_ASSERT_EQUAL_UINT32(30000, porLlenar);

    for (int i = 0; i < 7; ++i) --porLlenar;
    buzon.pedirCancelacion();                  // llega a mitad de la tanda
    for (int i = 0; i < 13; ++i) --porLlenar;  // la tanda termina sin verla

    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Cancelar);
    TEST_ASSERT_EQUAL_UINT32(0, porLlenar);
    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Ninguna);
    TEST_ASSERT_EQUAL_UINT32(0, porLlenar);
}

void test_si_llegan_dos_ordenes_vale_la_ultima(void) {
    core::BuzonLlenadoBanco buzon;
    uint32_t porLlenar = 0;
    buzon.pedirLlenado(100);
    buzon.pedirCancelacion();
    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Cancelar);
    TEST_ASSERT_EQUAL_UINT32(0, porLlenar);

    buzon.pedirCancelacion();
    buzon.pedirLlenado(50);
    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Llenar);
    TEST_ASSERT_EQUAL_UINT32(50, porLlenar);
}

void test_sin_orden_el_contador_no_cambia(void) {
    core::BuzonLlenadoBanco buzon;
    uint32_t porLlenar = 30;
    buzon.pedirLlenado(0);  // fuera de rango: se ignora
    TEST_ASSERT_TRUE(buzon.aplicar(porLlenar) == core::OrdenBanco::Ninguna);
    TEST_ASSERT_EQUAL_UINT32(30, porLlenar);
}

void run_tests_llenado_banco(void) {
    RUN_TEST(test_la_tanda_respeta_el_maximo_y_lo_pedido);
    RUN_TEST(test_la_tanda_no_pasa_de_la_capacidad);
    RUN_TEST(test_el_registro_sintetico_va_marcado_y_en_rango);
    RUN_TEST(test_sin_sntp_el_sintetico_no_inventa_la_hora);
    RUN_TEST(test_cancelar_a_mitad_de_una_tanda_deja_el_contador_en_cero);
    RUN_TEST(test_si_llegan_dos_ordenes_vale_la_ultima);
    RUN_TEST(test_sin_orden_el_contador_no_cambia);
}
