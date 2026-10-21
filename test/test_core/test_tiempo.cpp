/**
 * Tiempo del nodo (HU-01): hora UTC solo si es confiable, nunca inventada.
 */
#include <unity.h>

#include "core/Tiempo.h"

using core::CalidadTiempo;

static const uint32_t EPOCH = 1790350200u;  // 2026-09-25T15:30:00Z

void test_tiempo_formato_iso8601(void) {
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", core::formatearISO8601(EPOCH).c_str());
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", core::formatearISO8601(0).c_str());
    TEST_ASSERT_EQUAL_STRING("2024-02-29T12:00:00Z", core::formatearISO8601(1709208000u).c_str());
}

void test_tiempo_calidades(void) {
    core::BaseTiempo base;
    uint32_t epoch = 1;

    // Con SNTP en la captura: se usa tal cual.
    TEST_ASSERT_TRUE(core::resolverTiempo(EPOCH, 42, 100, base, epoch) == CalidadTiempo::Sincronizado);
    TEST_ASSERT_EQUAL_UINT32(EPOCH, epoch);

    // Sin SNTP y sin base: no se inventa la hora.
    TEST_ASSERT_TRUE(core::resolverTiempo(0, 42, 100, base, epoch) == CalidadTiempo::SinSincronizar);
    TEST_ASSERT_EQUAL_UINT32(0, epoch);

    // SNTP llegó después en el mismo arranque: se retrofecha con el uptime.
    base.fijar(42, EPOCH, 400);
    TEST_ASSERT_TRUE(core::resolverTiempo(0, 42, 100, base, epoch) == CalidadTiempo::Retrofechado);
    TEST_ASSERT_EQUAL_UINT32(EPOCH - 300, epoch);

    // Otro arranque: el uptime no es comparable.
    TEST_ASSERT_TRUE(core::resolverTiempo(0, 43, 100, base, epoch) == CalidadTiempo::SinSincronizar);
}

void test_tiempo_nombres_de_calidad(void) {
    TEST_ASSERT_EQUAL_STRING("sntp_synced", core::nombreCalidadTiempo(CalidadTiempo::Sincronizado));
    TEST_ASSERT_EQUAL_STRING("sntp_backfilled", core::nombreCalidadTiempo(CalidadTiempo::Retrofechado));
    TEST_ASSERT_EQUAL_STRING("unsynced", core::nombreCalidadTiempo(CalidadTiempo::SinSincronizar));
}

/// IOT-007: dentro de un arranque, SNTP no puede mover la hora a voluntad.
void test_tiempo_primera_sincronizacion_se_acepta(void) {
    core::BaseTiempo base;
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1790000000u, 100) ==
                     core::DecisionSincronizacion::Aceptar);
}

void test_tiempo_deriva_normal_se_acepta(void) {
    core::BaseTiempo base;
    base.fijar(7, 1790000000u, 100);
    // Una hora después, 50 ms de deriva redondeados a 0/1 s.
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1790003601u, 3700) ==
                     core::DecisionSincronizacion::Aceptar);
}

void test_tiempo_salto_hacia_atras_o_adelante_se_rechaza(void) {
    core::BaseTiempo base;
    base.fijar(7, 1790000000u, 100);
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1790000000u - 3600, 200) ==
                     core::DecisionSincronizacion::SaltoRechazado);
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1790000000u + 7200, 200) ==
                     core::DecisionSincronizacion::SaltoRechazado);
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1790000000u + 100 + core::TOLERANCIA_SALTO_RELOJ_S, 200) ==
                     core::DecisionSincronizacion::Aceptar);
}

void test_tiempo_base_de_otro_arranque_no_cuenta(void) {
    core::BaseTiempo base;
    base.fijar(6, 1790000000u, 100);
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1795000000u, 50) ==
                     core::DecisionSincronizacion::Aceptar);
}

void test_tiempo_epoca_implausible_se_rechaza(void) {
    core::BaseTiempo base;
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, 1000u, 10) ==
                     core::DecisionSincronizacion::FueraDeRango);
    TEST_ASSERT_TRUE(core::evaluarSincronizacion(base, 7, core::EPOCH_MAXIMA_PLAUSIBLE + 1u, 10) ==
                     core::DecisionSincronizacion::FueraDeRango);
}

void run_tests_tiempo(void) {
    RUN_TEST(test_tiempo_primera_sincronizacion_se_acepta);
    RUN_TEST(test_tiempo_deriva_normal_se_acepta);
    RUN_TEST(test_tiempo_salto_hacia_atras_o_adelante_se_rechaza);
    RUN_TEST(test_tiempo_base_de_otro_arranque_no_cuenta);
    RUN_TEST(test_tiempo_epoca_implausible_se_rechaza);
    RUN_TEST(test_tiempo_formato_iso8601);
    RUN_TEST(test_tiempo_calidades);
    RUN_TEST(test_tiempo_nombres_de_calidad);
}
