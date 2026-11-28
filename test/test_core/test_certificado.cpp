/**
 * HU-09 esc. 2: vigencia del certificado del broker (mbedTLS del core no
 * comprueba las fechas; ver core/Certificado.h).
 */
#include <unity.h>

#include "core/Certificado.h"
#include "core/Tiempo.h"

using core::FechaX509;
using core::VigenciaCertificado;

static FechaX509 fecha(int anio, int mes, int dia, int hora = 0, int minuto = 0, int segundo = 0) {
    FechaX509 f;
    f.anio = anio;
    f.mes = mes;
    f.dia = dia;
    f.hora = hora;
    f.minuto = minuto;
    f.segundo = segundo;
    return f;
}

// Certificado de ejemplo: válido del 2026-01-15 al 2027-01-15 (UTC).
static const FechaX509 DESDE = fecha(2026, 1, 15);
static const FechaX509 HASTA = fecha(2027, 1, 15, 23, 59, 59);
static const uint32_t EPOCH_28_11_2026 = 1795824000u;  // 2026-11-28T00:00:00Z

void test_epoch_de_fechas_conocidas(void) {
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(1970, 1, 1)) == 0);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(2024, 1, 1)) == (int64_t)core::EPOCH_MINIMA_VALIDA);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(2024, 2, 29, 12)) == 1709208000LL);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(2026, 11, 28)) == (int64_t)EPOCH_28_11_2026);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(2049, 12, 31, 23, 59, 59)) == 2524607999LL);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(1950, 1, 1)) == -631152000LL);
    TEST_ASSERT_TRUE(core::epochDesdeFechaX509(fecha(9999, 12, 31, 23, 59, 59)) == 253402300799LL);
}

void test_fechas_fuera_de_rango_son_invalidas(void) {
    TEST_ASSERT_TRUE(core::fechaX509Valida(fecha(2024, 2, 29)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 2, 29)));  // 2026 no es bisiesto
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2100, 2, 29)));  // múltiplo de 100
    TEST_ASSERT_TRUE(core::fechaX509Valida(fecha(2000, 2, 29)));   // múltiplo de 400
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 0, 10)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 13, 10)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 4, 31)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 4, 30, 24)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 4, 30, 23, 60)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(2026, 4, 30, 23, 59, 60)));
    TEST_ASSERT_FALSE(core::fechaX509Valida(fecha(1949, 12, 31)));
}

void test_certificado_vigente(void) {
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, EPOCH_28_11_2026) == VigenciaCertificado::Vigente);
}

void test_certificado_vencido_se_rechaza(void) {
    const FechaX509 vencioAyer = fecha(2026, 11, 27, 23, 59, 59);
    const VigenciaCertificado v = core::evaluarVigencia(DESDE, vencioAyer, EPOCH_28_11_2026);
    TEST_ASSERT_TRUE(v == VigenciaCertificado::Vencido);
    TEST_ASSERT_TRUE(core::vigenciaRechaza(v));
    TEST_ASSERT_EQUAL_STRING("vencido", core::nombreVigencia(v));
}

void test_certificado_aun_no_valido_se_rechaza(void) {
    const VigenciaCertificado v = core::evaluarVigencia(fecha(2026, 12, 1), HASTA, EPOCH_28_11_2026);
    TEST_ASSERT_TRUE(v == VigenciaCertificado::AunNoValido);
    TEST_ASSERT_TRUE(core::vigenciaRechaza(v));
}

void test_los_extremos_del_periodo_son_inclusivos(void) {
    const uint32_t inicio = (uint32_t)core::epochDesdeFechaX509(DESDE);
    const uint32_t fin = (uint32_t)core::epochDesdeFechaX509(HASTA);
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, inicio) == VigenciaCertificado::Vigente);
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, fin) == VigenciaCertificado::Vigente);
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, inicio - 1) == VigenciaCertificado::AunNoValido);
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, fin + 1) == VigenciaCertificado::Vencido);
}

void test_sin_hora_sntp_no_decide(void) {
    // Antes de SNTP el ESP32 cuenta desde 1970: no se puede afirmar que venció.
    const VigenciaCertificado v = core::evaluarVigencia(DESDE, fecha(2025, 1, 1), 0);
    TEST_ASSERT_TRUE(v == VigenciaCertificado::SinHora);
    TEST_ASSERT_FALSE(core::vigenciaRechaza(v));
    TEST_ASSERT_TRUE(core::evaluarVigencia(DESDE, HASTA, core::EPOCH_MINIMA_VALIDA - 1) == VigenciaCertificado::SinHora);
}

void test_fecha_corrupta_se_rechaza_aun_sin_hora(void) {
    TEST_ASSERT_TRUE(core::evaluarVigencia(fecha(2026, 2, 30), HASTA, 0) == VigenciaCertificado::FechaInvalida);
    // notBefore posterior a notAfter.
    const VigenciaCertificado v = core::evaluarVigencia(HASTA, DESDE, EPOCH_28_11_2026);
    TEST_ASSERT_TRUE(v == VigenciaCertificado::FechaInvalida);
    TEST_ASSERT_TRUE(core::vigenciaRechaza(v));
}

void test_la_cadena_toma_la_peor_vigencia(void) {
    using V = VigenciaCertificado;
    TEST_ASSERT_TRUE(core::peorVigencia(V::Vigente, V::Vigente) == V::Vigente);
    TEST_ASSERT_TRUE(core::peorVigencia(V::Vigente, V::SinHora) == V::SinHora);
    TEST_ASSERT_TRUE(core::peorVigencia(V::Vencido, V::Vigente) == V::Vencido);  // intermedia vencida
    TEST_ASSERT_TRUE(core::peorVigencia(V::AunNoValido, V::Vencido) == V::Vencido);
    TEST_ASSERT_TRUE(core::peorVigencia(V::Vencido, V::FechaInvalida) == V::FechaInvalida);
}

void run_tests_certificado(void) {
    RUN_TEST(test_epoch_de_fechas_conocidas);
    RUN_TEST(test_fechas_fuera_de_rango_son_invalidas);
    RUN_TEST(test_certificado_vigente);
    RUN_TEST(test_certificado_vencido_se_rechaza);
    RUN_TEST(test_certificado_aun_no_valido_se_rechaza);
    RUN_TEST(test_los_extremos_del_periodo_son_inclusivos);
    RUN_TEST(test_sin_hora_sntp_no_decide);
    RUN_TEST(test_fecha_corrupta_se_rechaza_aun_sin_hora);
    RUN_TEST(test_la_cadena_toma_la_peor_vigencia);
}
