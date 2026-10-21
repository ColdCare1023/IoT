/**
 * IOT-004: reglas de la actualización OTA (orden, versión, huella, arranque).
 */
#include <unity.h>

#include "core/Ota.h"
#include "core/Sha256.h"

static const char* HUELLA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static std::string orden(const std::string& device, const std::string& version, const std::string& url,
                         const std::string& sha, const std::string& tamano) {
    return "{\"device_id\":\"" + device + "\",\"version\":\"" + version + "\",\"url\":\"" + url +
           "\",\"sha256\":\"" + sha + "\",\"tamano\":" + tamano + "}";
}

void test_ota_version_se_parsea_y_compara(void) {
    core::VersionFirmware a, b;
    TEST_ASSERT_TRUE(core::parsearVersion("1.2.0", a));
    TEST_ASSERT_TRUE(core::parsearVersion("1.10.0", b));
    TEST_ASSERT_TRUE(core::esPosterior(b, a));   // 10 > 2, no orden de texto
    TEST_ASSERT_FALSE(core::esPosterior(a, b));
    TEST_ASSERT_FALSE(core::esPosterior(a, a));  // misma versión: no es actualización
    TEST_ASSERT_FALSE(core::parsearVersion("v1.2.0", a));
    TEST_ASSERT_FALSE(core::parsearVersion("1.2", a));
    TEST_ASSERT_FALSE(core::parsearVersion("1.2.0-rc1", a));
    TEST_ASSERT_FALSE(core::parsearVersion("1.2.99999", a));
}

void test_ota_orden_valida(void) {
    core::OrdenOTA o;
    std::string motivo;
    TEST_ASSERT_TRUE(core::parsearOrdenOTA(
        orden("FARM-01", "1.3.0", "https://firmware.example.org/coldcare-1.3.0.bin", HUELLA, "1100000"),
        "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("1.3.0", o.version.c_str());
    TEST_ASSERT_EQUAL_UINT32(1100000, o.tamano);
}

void test_ota_orden_invalida_se_rechaza_con_motivo(void) {
    core::OrdenOTA o;
    std::string motivo;
    const std::string url = "https://firmware.example.org/f.bin";
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("OTRO", "1.3.0", url, HUELLA, "100"), "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("device_id", motivo.c_str());
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("FARM-01", "1.3.0", "http://x/f.bin", HUELLA, "100"), "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("url", motivo.c_str());
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("FARM-01", "1.3.0", url, "abc", "100"), "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("sha256", motivo.c_str());
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("FARM-01", "1.3.0", url, HUELLA, "0"), "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("tamano", motivo.c_str());
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("FARM-01", "1.3.0", url, HUELLA, "99999999"), "FARM-01", o, motivo));
    TEST_ASSERT_FALSE(core::parsearOrdenOTA(orden("FARM-01", "uno", url, HUELLA, "100"), "FARM-01", o, motivo));
    TEST_ASSERT_EQUAL_STRING("version", motivo.c_str());
}

void test_ota_sha256_por_partes_igual_que_de_una_vez(void) {
    std::string datos;
    for (int i = 0; i < 1000; ++i) datos += (char)('a' + i % 26);
    core::Sha256 calculo;
    for (size_t i = 0; i < datos.size(); i += 37) {
        const size_t largo = (datos.size() - i) < 37 ? (datos.size() - i) : 37;
        calculo.actualizar(reinterpret_cast<const uint8_t*>(datos.data() + i), largo);
    }
    TEST_ASSERT_EQUAL_STRING(core::sha256Hex(datos).c_str(), calculo.finalizarHex().c_str());
    // Tras finalizar, el objeto vuelve a empezar.
    calculo.actualizar(reinterpret_cast<const uint8_t*>("abc"), 3);
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             calculo.finalizarHex().c_str());
}

void test_ota_arranque_a_prueba(void) {
    using core::DecisionArranque;
    const uint32_t plazo = 15u * 60u * 1000u;
    TEST_ASSERT_TRUE(core::evaluarArranque(false, false, false, 0, plazo) == DecisionArranque::Confirmar);
    TEST_ASSERT_TRUE(core::evaluarArranque(true, false, false, 1000, plazo) == DecisionArranque::Esperar);
    TEST_ASSERT_TRUE(core::evaluarArranque(true, true, false, 1000, plazo) == DecisionArranque::Esperar);
    TEST_ASSERT_TRUE(core::evaluarArranque(true, true, true, 1000, plazo) == DecisionArranque::Confirmar);
    TEST_ASSERT_TRUE(core::evaluarArranque(true, true, false, plazo, plazo) == DecisionArranque::Revertir);
}

void run_tests_ota(void) {
    RUN_TEST(test_ota_version_se_parsea_y_compara);
    RUN_TEST(test_ota_orden_valida);
    RUN_TEST(test_ota_orden_invalida_se_rechaza_con_motivo);
    RUN_TEST(test_ota_sha256_por_partes_igual_que_de_una_vez);
    RUN_TEST(test_ota_arranque_a_prueba);
}
