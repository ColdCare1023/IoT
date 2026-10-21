#include <unity.h>

#include "core/Pem.h"

using core::base64Valido;
using core::pemDesdeBase64;

static void test_base64_valido_e_invalido() {
    TEST_ASSERT_TRUE(base64Valido("TWFu"));
    TEST_ASSERT_TRUE(base64Valido("TWE="));
    TEST_ASSERT_TRUE(base64Valido("TQ=="));
    TEST_ASSERT_TRUE(base64Valido("ab+/"));
    TEST_ASSERT_FALSE(base64Valido(""));
    TEST_ASSERT_FALSE(base64Valido("TWF"));       // largo no múltiplo de 4
    TEST_ASSERT_FALSE(base64Valido("TW=u"));      // relleno en medio
    TEST_ASSERT_FALSE(base64Valido("T==="));      // demasiado relleno
    TEST_ASSERT_FALSE(base64Valido("TW u"));      // espacio
    TEST_ASSERT_FALSE(base64Valido("TW-_"));      // alfabeto URL-safe
    TEST_ASSERT_FALSE(base64Valido("TWFuTWFu", 4));  // supera el máximo
}

static void test_pem_en_lineas_de_64() {
    const std::string b64(100, 'A');
    const std::string pem = pemDesdeBase64("CERTIFICATE", b64);
    const std::string esperado = "-----BEGIN CERTIFICATE-----\n" + std::string(64, 'A') + "\n" +
                                 std::string(36, 'A') + "\n-----END CERTIFICATE-----\n";
    TEST_ASSERT_EQUAL_STRING(esperado.c_str(), pem.c_str());
}

static void test_pem_de_base64_invalido_es_vacio() {
    TEST_ASSERT_TRUE(pemDesdeBase64("CERTIFICATE", "no es base64").empty());
    // Una línea pegada con cabeceras PEM no se acepta: solo el cuerpo base64.
    TEST_ASSERT_TRUE(pemDesdeBase64("CERTIFICATE", "-----BEGIN CERTIFICATE-----").empty());
}

void run_tests_pem() {
    RUN_TEST(test_base64_valido_e_invalido);
    RUN_TEST(test_pem_en_lineas_de_64);
    RUN_TEST(test_pem_de_base64_invalido_es_vacio);
}
