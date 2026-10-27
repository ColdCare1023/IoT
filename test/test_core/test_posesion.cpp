/**
 * HU-44: prueba de posesión de la credencial y muestra vigente.
 * Los valores esperados se calcularon con hmac/hashlib de Python, que es lo
 * que usa el backend para verificar.
 */
#include <unity.h>

#include <string>

#include "core/Posesion.h"
#include "core/Sensores.h"

static const char* NONCE = "00112233445566778899aabbccddeeff";

void test_hmac_sha256_vectores_rfc4231(void) {
    const std::string clave = "Jefe";
    TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                             core::hmacSha256Hex(reinterpret_cast<const uint8_t*>(clave.data()), clave.size(),
                                                 "what do ya want for nothing?").c_str());
    uint8_t larga[131];
    for (auto& b : larga) b = 0xaa;
    TEST_ASSERT_EQUAL_STRING("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
                             core::hmacSha256Hex(larga, sizeof(larga),
                                                 "Test Using Larger Than Block-Size Key - Hash Key First").c_str());
}

void test_respuesta_coincide_con_la_del_backend(void) {
    TEST_ASSERT_EQUAL_STRING("3e1f00b74e04846aece33540dc379e778603f1cfcaeedcbc3106b578a25c9460",
                             core::respuestaPosesion("token-nuevo-de-prueba", "N1", NONCE).c_str());
}

void test_con_el_token_anterior_la_respuesta_es_otra(void) {
    const std::string nueva = core::respuestaPosesion("token-nuevo-de-prueba", "N1", NONCE);
    const std::string vieja = core::respuestaPosesion("token-viejo-de-prueba", "N1", NONCE);
    TEST_ASSERT_EQUAL_STRING("b55e92abc59bd5b7f8b0fe1d8ca9460022aebe27255bd49f2c282d508089e34e", vieja.c_str());
    TEST_ASSERT_TRUE(nueva != vieja);
}

void test_desafio_valido_se_acepta(void) {
    std::string nonce;
    TEST_ASSERT_TRUE(core::parsearDesafioPosesion(
        std::string("{\"device_id\":\"N1\",\"tipo\":\"desafio_posesion\",\"nonce\":\"") + NONCE + "\"}", "N1", nonce));
    TEST_ASSERT_EQUAL_STRING(NONCE, nonce.c_str());
}

void test_desafio_ajeno_o_mal_formado_se_ignora(void) {
    std::string nonce;
    const std::string otro = std::string("{\"device_id\":\"N2\",\"tipo\":\"desafio_posesion\",\"nonce\":\"") + NONCE + "\"}";
    TEST_ASSERT_FALSE(core::parsearDesafioPosesion(otro, "N1", nonce));
    TEST_ASSERT_FALSE(core::parsearDesafioPosesion(
        "{\"device_id\":\"N1\",\"tipo\":\"desafio_posesion\",\"nonce\":\"corto\"}", "N1", nonce));
    TEST_ASSERT_FALSE(core::parsearDesafioPosesion(
        "{\"device_id\":\"N1\",\"tipo\":\"desafio_posesion\",\"nonce\":\"ZZ112233445566778899aabbccddeeff\"}", "N1",
        nonce));
    // Un acuse normal no es un desafío.
    TEST_ASSERT_FALSE(core::parsearDesafioPosesion(
        "{\"device_id\":\"N1\",\"boot_id\":3,\"seq_no\":9,\"status\":\"committed\"}", "N1", nonce));
}

void test_lectura_vigente_con_y_sin_valor(void) {
    core::RegistroLectura r;
    r.bootId = 7;
    r.seqNo = 41;
    r.uptimeS = 120;
    r.tempDsCenti = 912;
    r.falloDs = core::FalloSensor::Ninguno;
    r.capturadoEpoch = 1793100000u;
    r.poner(core::BANDERA_TIEMPO_SNTP, true);
    const std::string d = core::detalleLecturaVigente(r);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, d.find("\"temperatura_interna\":9.12"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, d.find("\"seq_no\":41"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, d.find("\"time_quality\":\"synced\""));

    r.falloDs = core::FalloSensor::SinRespuesta;
    r.poner(core::BANDERA_TIEMPO_SNTP, false);
    const std::string f = core::detalleLecturaVigente(r);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.find("\"temperatura_interna\":null"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.find("\"captured_at\":null"));
}

void run_tests_posesion(void) {
    RUN_TEST(test_hmac_sha256_vectores_rfc4231);
    RUN_TEST(test_respuesta_coincide_con_la_del_backend);
    RUN_TEST(test_con_el_token_anterior_la_respuesta_es_otra);
    RUN_TEST(test_desafio_valido_se_acepta);
    RUN_TEST(test_desafio_ajeno_o_mal_formado_se_ignora);
    RUN_TEST(test_lectura_vigente_con_y_sin_valor);
}
