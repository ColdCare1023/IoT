/**
 * Estadística del tiempo captura → guardado en LittleFS (BUF-01, RNF-01).
 */
#include <unity.h>

#include "core/TiempoGuardado.h"

void test_sin_lecturas_todo_en_cero(void) {
    core::TiempoGuardado t;
    TEST_ASSERT_EQUAL_UINT32(0, t.n);
    TEST_ASSERT_EQUAL_UINT32(0, t.mediaMs());
    TEST_ASSERT_EQUAL_UINT32(0, t.maximoMs);
}

void test_minimo_maximo_media_y_ultimo(void) {
    core::TiempoGuardado t;
    t.registrar(800, true);
    t.registrar(1200, true);
    t.registrar(1000, true);
    TEST_ASSERT_EQUAL_UINT32(3, t.n);
    TEST_ASSERT_EQUAL_UINT32(800, t.minimoMs);
    TEST_ASSERT_EQUAL_UINT32(1200, t.maximoMs);
    TEST_ASSERT_EQUAL_UINT32(1000, t.mediaMs());
    TEST_ASSERT_EQUAL_UINT32(1000, t.ultimoMs);
}

void test_el_limite_de_5_s_es_inclusivo(void) {
    core::TiempoGuardado t;
    t.registrar(5000, true);  // justo 5 s cumple
    t.registrar(5001, true);
    TEST_ASSERT_EQUAL_UINT32(1, t.sobreLimite);
    TEST_ASSERT_EQUAL_UINT32(1, t.tramos[2]);
    TEST_ASSERT_EQUAL_UINT32(1, t.tramos[3]);
}

void test_tramos_del_histograma(void) {
    core::TiempoGuardado t;
    t.registrar(999, true);
    t.registrar(1000, true);
    t.registrar(1999, true);
    t.registrar(2000, true);
    TEST_ASSERT_EQUAL_UINT32(1, t.tramos[0]);
    TEST_ASSERT_EQUAL_UINT32(2, t.tramos[1]);
    TEST_ASSERT_EQUAL_UINT32(1, t.tramos[2]);
    TEST_ASSERT_EQUAL_UINT32(0, t.tramos[3]);
}

void test_las_capturas_no_guardadas_se_cuentan_aparte(void) {
    core::TiempoGuardado t;
    t.registrar(900, true);
    t.registrar(60000, false);
    TEST_ASSERT_EQUAL_UINT32(1, t.n);
    TEST_ASSERT_EQUAL_UINT32(1, t.fallidas);
    TEST_ASSERT_EQUAL_UINT32(900, t.maximoMs);
    TEST_ASSERT_EQUAL_UINT32(0, t.sobreLimite);
}

void run_tests_tiempo_guardado(void) {
    RUN_TEST(test_sin_lecturas_todo_en_cero);
    RUN_TEST(test_minimo_maximo_media_y_ultimo);
    RUN_TEST(test_el_limite_de_5_s_es_inclusivo);
    RUN_TEST(test_tramos_del_histograma);
    RUN_TEST(test_las_capturas_no_guardadas_se_cuentan_aparte);
}
