/**
 * IOT-005: el firmware demo nunca hereda la identidad productiva de NVS.
 */
#include <unity.h>

#include "core/Identidad.h"

void test_produccion_usa_el_device_id_de_nvs(void) {
    const core::Identidad id = core::resolverIdentidad("ESP32_FARMACIA_01", "FARM-01-CDL", false);
    TEST_ASSERT_EQUAL_STRING("FARM-01-CDL", id.deviceId.c_str());
    TEST_ASSERT_FALSE(id.bloqueada);
}

void test_produccion_sin_nvs_usa_el_compilado(void) {
    const core::Identidad id = core::resolverIdentidad("ESP32_FARMACIA_01", "", false);
    TEST_ASSERT_EQUAL_STRING("ESP32_FARMACIA_01", id.deviceId.c_str());
    TEST_ASSERT_FALSE(id.bloqueada);
}

void test_demo_sobre_nodo_productivo_queda_bloqueado(void) {
    const core::Identidad id = core::resolverIdentidad("ESP32_DEMO_01", "FARM-01-CDL", true);
    TEST_ASSERT_EQUAL_STRING("ESP32_DEMO_01", id.deviceId.c_str());
    TEST_ASSERT_TRUE(id.bloqueada);
}

void test_demo_en_su_propia_placa_publica(void) {
    TEST_ASSERT_FALSE(core::resolverIdentidad("ESP32_DEMO_01", "", true).bloqueada);
    TEST_ASSERT_FALSE(core::resolverIdentidad("ESP32_DEMO_01", "ESP32_DEMO_01", true).bloqueada);
}

void test_un_device_id_invalido_en_nvs_se_ignora(void) {
    const core::Identidad id = core::resolverIdentidad("ESP32_DEMO_01", "id con espacios", true);
    TEST_ASSERT_EQUAL_STRING("ESP32_DEMO_01", id.deviceId.c_str());
    TEST_ASSERT_FALSE(id.bloqueada);
}

void run_tests_identidad(void) {
    RUN_TEST(test_produccion_usa_el_device_id_de_nvs);
    RUN_TEST(test_produccion_sin_nvs_usa_el_compilado);
    RUN_TEST(test_demo_sobre_nodo_productivo_queda_bloqueado);
    RUN_TEST(test_demo_en_su_propia_placa_publica);
    RUN_TEST(test_un_device_id_invalido_en_nvs_se_ignora);
}
