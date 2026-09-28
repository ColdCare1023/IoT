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

void run_tests_tiempo(void) {
    RUN_TEST(test_tiempo_formato_iso8601);
    RUN_TEST(test_tiempo_calidades);
    RUN_TEST(test_tiempo_nombres_de_calidad);
}
