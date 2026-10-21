/**
 * IOT-002 (consola bloqueada) e IOT-008 (huellas): SHA-256 y política.
 */
#include <unity.h>

#include "core/Consola.h"
#include "core/Sha256.h"

void test_sha256_vectores_fips(void) {
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                             core::sha256Hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             core::sha256Hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                             core::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

void test_sha256_cruza_el_limite_de_bloque(void) {
    // 55, 56 y 64 bytes: el relleno cae en uno o dos bloques.
    TEST_ASSERT_EQUAL_STRING("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
                             core::sha256Hex(std::string(55, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
                             core::sha256Hex(std::string(56, 'a')).c_str());
    TEST_ASSERT_EQUAL_STRING("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
                             core::sha256Hex(std::string(64, 'a')).c_str());
}

void test_comparacion_en_tiempo_constante(void) {
    TEST_ASSERT_TRUE(core::igualesTiempoConstante("abc", "abc"));
    TEST_ASSERT_FALSE(core::igualesTiempoConstante("abc", "abd"));
    TEST_ASSERT_FALSE(core::igualesTiempoConstante("abc", "abcd"));
}

void test_solo_los_comandos_de_lectura_no_modifican(void) {
    TEST_ASSERT_FALSE(core::comandoModifica("estado"));
    TEST_ASSERT_FALSE(core::comandoModifica("rssi"));
    TEST_ASSERT_FALSE(core::comandoModifica("ayuda"));
    TEST_ASSERT_FALSE(core::comandoModifica("desbloquear"));
    TEST_ASSERT_TRUE(core::comandoModifica("nvs"));
    TEST_ASSERT_TRUE(core::comandoModifica("reiniciar"));
    TEST_ASSERT_TRUE(core::comandoModifica("demo"));
    TEST_ASSERT_TRUE(core::comandoModifica("desconocido"));
}

void test_clave_de_servicio_se_guarda_como_huella(void) {
    const std::string huella = core::huellaClaveServicio("clave-de-servicio-01");
    TEST_ASSERT_EQUAL(64, huella.size());
    TEST_ASSERT_TRUE(huella.find("clave") == std::string::npos);
    TEST_ASSERT_TRUE(core::claveServicioCorrecta("clave-de-servicio-01", huella));
    TEST_ASSERT_FALSE(core::claveServicioCorrecta("clave-de-servicio-02", huella));
    TEST_ASSERT_FALSE(core::claveServicioCorrecta("clave-de-servicio-01", ""));
}

void test_clave_de_servicio_demasiado_corta_se_rechaza(void) {
    TEST_ASSERT_FALSE(core::claveServicioAceptable("corta"));
    TEST_ASSERT_FALSE(core::claveServicioAceptable("con espacios dentro"));
    TEST_ASSERT_TRUE(core::claveServicioAceptable("suficiente-larga"));
}

void test_consola_bloqueada_solo_con_nodo_aprovisionado_y_clave(void) {
    core::EstadoConsola consola;
    TEST_ASSERT_FALSE(consola.bloqueada(false, true, 0));
    TEST_ASSERT_FALSE(consola.bloqueada(true, false, 0));
    TEST_ASSERT_TRUE(consola.bloqueada(true, true, 0));
}

void test_el_desbloqueo_caduca(void) {
    core::EstadoConsola consola;
    consola.desbloquear(1000);
    TEST_ASSERT_FALSE(consola.bloqueada(true, true, 1000 + core::EstadoConsola::VENTANA_MS - 1));
    TEST_ASSERT_TRUE(consola.bloqueada(true, true, 1000 + core::EstadoConsola::VENTANA_MS));
    consola.desbloquear(5000);
    consola.bloquear();
    TEST_ASSERT_TRUE(consola.bloqueada(true, true, 5001));
}

void test_freno_de_intentos(void) {
    core::FrenoIntentos freno;
    for (uint32_t i = 0; i < core::FrenoIntentos::MAX_INTENTOS; ++i) {
        TEST_ASSERT_TRUE(freno.puedeIntentar(100));
        freno.registrarFallo(100);
    }
    TEST_ASSERT_FALSE(freno.puedeIntentar(101));
    TEST_ASSERT_TRUE(freno.puedeIntentar(100 + core::FrenoIntentos::ESPERA_MS));
    freno.registrarAcierto();
    TEST_ASSERT_TRUE(freno.puedeIntentar(102));
}

void run_tests_consola(void) {
    RUN_TEST(test_sha256_vectores_fips);
    RUN_TEST(test_sha256_cruza_el_limite_de_bloque);
    RUN_TEST(test_comparacion_en_tiempo_constante);
    RUN_TEST(test_solo_los_comandos_de_lectura_no_modifican);
    RUN_TEST(test_clave_de_servicio_se_guarda_como_huella);
    RUN_TEST(test_clave_de_servicio_demasiado_corta_se_rechaza);
    RUN_TEST(test_consola_bloqueada_solo_con_nodo_aprovisionado_y_clave);
    RUN_TEST(test_el_desbloqueo_caduca);
    RUN_TEST(test_freno_de_intentos);
}
