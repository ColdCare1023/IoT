/**
 * Decodificación y CRC de DS18B20 / SHT31 (HU-01, HU-02).
 *
 * Lo que se fija aquí: ningún código de error ni trama corrupta puede salir
 * como temperatura. Es la condición para que el backend jamás calcule una
 * excursión térmica falsa.
 */
#include <unity.h>

#include <cstring>

#include "core/Sensores.h"

using core::FalloSensor;

static void scratchpad(uint8_t sp[9], uint16_t raw, uint8_t config) {
    sp[0] = (uint8_t)(raw & 0xFF);
    sp[1] = (uint8_t)(raw >> 8);
    sp[2] = 0x4B;  // TH
    sp[3] = 0x46;  // TL
    sp[4] = config;
    sp[5] = 0xFF;
    sp[6] = 0x0C;
    sp[7] = 0x10;
    sp[8] = core::crc8Maxim(sp, 8);
}

static void tramaSht(uint8_t t[6], uint16_t st, uint16_t srh) {
    t[0] = (uint8_t)(st >> 8);
    t[1] = (uint8_t)(st & 0xFF);
    t[2] = core::crc8Sensirion(&t[0], 2);
    t[3] = (uint8_t)(srh >> 8);
    t[4] = (uint8_t)(srh & 0xFF);
    t[5] = core::crc8Sensirion(&t[3], 2);
}

// ── CRC: valores de verificación publicados ────────────────────────────────

void test_crc8_maxim_valor_de_verificacion(void) {
    const uint8_t datos[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xA1, core::crc8Maxim(datos, sizeof(datos)));
}

void test_crc8_sensirion_ejemplo_hoja_de_datos(void) {
    const uint8_t datos[] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, core::crc8Sensirion(datos, 2));
}

// ── DS18B20 ────────────────────────────────────────────────────────────────

void test_ds18b20_lecturas_de_la_hoja_de_datos(void) {
    uint8_t sp[9];
    scratchpad(sp, 0x0191, 0x7F);  // +25,0625 °C
    core::LecturaSensor l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_TRUE(l.valida());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 25.0625f, l.valor);

    scratchpad(sp, 0xFF5E, 0x7F);  // −10,125 °C
    l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_TRUE(l.valida());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -10.125f, l.valor);

    scratchpad(sp, 0x0044, 0x7F);  // +4,25 °C (rango de refrigeración)
    l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.25f, l.valor);
}

void test_ds18b20_crc_invalido_es_sensor_fault(void) {
    uint8_t sp[9];
    scratchpad(sp, 0x0044, 0x7F);
    sp[0] ^= 0x01;  // un bit invertido por ruido en el cable
    const core::LecturaSensor l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_FALSE(l.valida());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::ErrorCrc, (uint8_t)l.fallo);
    TEST_ASSERT_TRUE(l.valor != l.valor);  // NaN
}

void test_ds18b20_bus_sin_sensor_es_sin_respuesta(void) {
    uint8_t sp[9];
    std::memset(sp, 0xFF, sizeof(sp));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::SinRespuesta,
                            (uint8_t)core::decodificarScratchpadDS18B20(sp).fallo);
}

/// Nueve ceros tienen CRC 0: "pasarían" el checksum y darían 0,0 °C, que en
/// una cadena de frío es una excursión. Debe rechazarse antes del CRC.
void test_ds18b20_bus_en_corto_no_pasa_como_cero_grados(void) {
    uint8_t sp[9];
    std::memset(sp, 0x00, sizeof(sp));
    const core::LecturaSensor l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_FALSE(l.valida());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::SinRespuesta, (uint8_t)l.fallo);
}

void test_ds18b20_valor_power_on_85_grados_es_falla(void) {
    uint8_t sp[9];
    scratchpad(sp, 0x0550, 0x7F);
    const core::LecturaSensor l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_FALSE(l.valida());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::ValorPowerOn, (uint8_t)l.fallo);
}

void test_ds18b20_fuera_de_rango_fisico_es_implausible(void) {
    uint8_t sp[9];
    scratchpad(sp, 0x07F0, 0x7F);  // +127 °C > 125 °C
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::Implausible,
                            (uint8_t)core::decodificarScratchpadDS18B20(sp).fallo);
    scratchpad(sp, 0xFC80, 0x7F);  // −56 °C < −55 °C
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::Implausible,
                            (uint8_t)core::decodificarScratchpadDS18B20(sp).fallo);
    scratchpad(sp, 0x07D0, 0x7F);  // +125 °C exacto: válido
    TEST_ASSERT_TRUE(core::decodificarScratchpadDS18B20(sp).valida());
}

void test_ds18b20_resolucion_9_bits_enmascara_bits_indefinidos(void) {
    uint8_t sp[9];
    scratchpad(sp, 0x0197, 0x1F);  // 9 bits: los 3 bits bajos no están definidos
    const core::LecturaSensor l = core::decodificarScratchpadDS18B20(sp);
    TEST_ASSERT_TRUE(l.valida());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 25.0f, l.valor);
}

// ── SHT31 ──────────────────────────────────────────────────────────────────

void test_sht31_trama_valida(void) {
    uint8_t t[6];
    tramaSht(t, 0x6666, 0x8000);  // ≈25,00 °C
    const core::LecturaSHT31 l = core::decodificarSHT31(t);
    TEST_ASSERT_TRUE(l.temperatura.valida());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, l.temperatura.valor);
}

void test_sht31_crc_de_temperatura_es_sensor_fault(void) {
    uint8_t t[6];
    tramaSht(t, 0x6666, 0x8000);
    t[1] ^= 0x10;
    const core::LecturaSHT31 l = core::decodificarSHT31(t);
    TEST_ASSERT_FALSE(l.temperatura.valida());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::ErrorCrc, (uint8_t)l.temperatura.fallo);
}

void test_sht31_temperatura_bajo_el_minimo_es_implausible(void) {
    uint8_t t[6];
    tramaSht(t, 0x0000, 0x8000);  // −45 °C < −40 °C del rango del sensor
    const core::LecturaSHT31 l = core::decodificarSHT31(t);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)FalloSensor::Implausible, (uint8_t)l.temperatura.fallo);
}

void test_codigos_de_fallo_estables(void) {
    TEST_ASSERT_NULL(core::codigoFallo(FalloSensor::Ninguno));
    TEST_ASSERT_EQUAL_STRING("no_response", core::codigoFallo(FalloSensor::SinRespuesta));
    TEST_ASSERT_EQUAL_STRING("crc_error", core::codigoFallo(FalloSensor::ErrorCrc));
    TEST_ASSERT_EQUAL_STRING("timeout", core::codigoFallo(FalloSensor::Timeout));
    TEST_ASSERT_EQUAL_STRING("implausible", core::codigoFallo(FalloSensor::Implausible));
    TEST_ASSERT_EQUAL_STRING("power_on_reset", core::codigoFallo(FalloSensor::ValorPowerOn));
}

void run_tests_sensores(void) {
    RUN_TEST(test_crc8_maxim_valor_de_verificacion);
    RUN_TEST(test_crc8_sensirion_ejemplo_hoja_de_datos);
    RUN_TEST(test_ds18b20_lecturas_de_la_hoja_de_datos);
    RUN_TEST(test_ds18b20_crc_invalido_es_sensor_fault);
    RUN_TEST(test_ds18b20_bus_sin_sensor_es_sin_respuesta);
    RUN_TEST(test_ds18b20_bus_en_corto_no_pasa_como_cero_grados);
    RUN_TEST(test_ds18b20_valor_power_on_85_grados_es_falla);
    RUN_TEST(test_ds18b20_fuera_de_rango_fisico_es_implausible);
    RUN_TEST(test_ds18b20_resolucion_9_bits_enmascara_bits_indefinidos);
    RUN_TEST(test_sht31_trama_valida);
    RUN_TEST(test_sht31_crc_de_temperatura_es_sensor_fault);
    RUN_TEST(test_sht31_temperatura_bajo_el_minimo_es_implausible);
    RUN_TEST(test_codigos_de_fallo_estables);
}
