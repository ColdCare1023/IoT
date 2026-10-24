/**
 * Contrato de telemetría v1.0 (HU-05), registro binario y tiempo (HU-01).
 */
#include <unity.h>

#include <cmath>
#include <string>

#include "core/Json.h"
#include "core/Payload.h"
#include "core/Registro.h"
#include "core/Tiempo.h"

using core::FalloSensor;

static const std::string DEVICE = "ESP32_FARMACIA_01";
static const uint32_t EPOCH_EJEMPLO = 1790350200u;  // 2026-09-25T15:30:00Z

static bool contiene(const std::string& s, const char* fragmento) {
    return s.find(fragmento) != std::string::npos;
}

/// Registro equivalente al ejemplo del documento de responsabilidades.
static core::RegistroLectura registroEjemplo() {
    core::RegistroLectura r;
    r.bootId = 42;
    r.seqNo = 1205;
    r.capturadoEpoch = EPOCH_EJEMPLO;
    r.uptimeS = 36000;
    r.poner(core::BANDERA_TIEMPO_SNTP, true);
    core::fijarDS18B20(r, core::LecturaSensor::ok(4.25f));
    core::LecturaSHT31 sht;
    sht.temperatura = core::LecturaSensor::ok(21.10f);
    sht.humedad = core::LecturaSensor::ok(58.5f);
    core::fijarSHT31(r, sht);
    r.poner(core::BANDERA_MC38_INSTALADO, true);
    r.fwMayor = 1;
    r.fwMenor = 2;
    r.fwParche = 0;
    return r;
}

// ── JSON ───────────────────────────────────────────────────────────────────

void test_json_nan_e_infinito_salen_como_null(void) {
    core::ObjetoJSON o;
    o.real("a", NAN);
    o.real("b", INFINITY);
    o.real("c", -0.001, 2);
    TEST_ASSERT_EQUAL_STRING("{\"a\":null,\"b\":null,\"c\":0.00}", o.cerrar().c_str());
}

void test_json_escapa_cadenas(void) {
    core::ObjetoJSON o;
    o.cadena("k", "a\"b\\c\n");
    TEST_ASSERT_EQUAL_STRING("{\"k\":\"a\\\"b\\\\c\\n\"}", o.cerrar().c_str());
}

/// `json.dumps()` de Python pone un espacio tras ':' y ','. Un buscador
/// literal de `"clave":` no encontraría el campo y el acuse nunca llegaría.
void test_json_lee_formato_python_con_espacios(void) {
    const std::string py = "{\"device_id\": \"X\", \"boot_id\": 42, \"seq_no\": 7, \"ok\": true}";
    uint32_t v = 0;
    std::string s;
    TEST_ASSERT_TRUE(core::leerSinSigno32(py, "boot_id", v));
    TEST_ASSERT_EQUAL_UINT32(42, v);
    TEST_ASSERT_TRUE(core::leerSinSigno32(py, "seq_no", v));
    TEST_ASSERT_EQUAL_UINT32(7, v);
    TEST_ASSERT_TRUE(core::leerCadena(py, "device_id", s));
    TEST_ASSERT_EQUAL_STRING("X", s.c_str());
}

void test_json_no_confunde_claves_anidadas_ni_valores(void) {
    const std::string j = "{\"meta\":{\"seq_no\":99},\"nota\":\"seq_no\",\"seq_no\":5}";
    uint32_t v = 0;
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "seq_no", v));
    TEST_ASSERT_EQUAL_UINT32(5, v);
}

void test_json_rechaza_numeros_no_uint32(void) {
    uint32_t v = 0;
    TEST_ASSERT_FALSE(core::leerSinSigno32("{\"n\":-1}", "n", v));
    TEST_ASSERT_FALSE(core::leerSinSigno32("{\"n\":1.5}", "n", v));
    TEST_ASSERT_FALSE(core::leerSinSigno32("{\"n\":4294967296}", "n", v));
    TEST_ASSERT_FALSE(core::leerSinSigno32("{\"n\":\"3\"}", "n", v));
    TEST_ASSERT_TRUE(core::leerSinSigno32("{\"n\":4294967295}", "n", v));
    TEST_ASSERT_FALSE(core::leerSinSigno32("{\"n\":", "n", v));  // truncado
}

// ── Registro binario ───────────────────────────────────────────────────────

void test_crc32_valor_de_verificacion(void) {
    const uint8_t d[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, core::crc32(d, sizeof(d)));
}

void test_registro_ida_y_vuelta(void) {
    core::RegistroLectura a = registroEjemplo();
    a.puertaAbiertaS = 17;
    a.puertaRachaS = 95;
    uint8_t buf[core::TAM_REGISTRO];
    core::codificarRegistro(a, buf);

    core::RegistroLectura b;
    TEST_ASSERT_TRUE(core::decodificarRegistro(buf, b));
    TEST_ASSERT_EQUAL_UINT32(42, b.bootId);
    TEST_ASSERT_EQUAL_UINT32(1205, b.seqNo);
    TEST_ASSERT_EQUAL_UINT32(EPOCH_EJEMPLO, b.capturadoEpoch);
    TEST_ASSERT_EQUAL_INT16(425, b.tempDsCenti);
    TEST_ASSERT_EQUAL_INT16(2110, b.tempShtCenti);
    TEST_ASSERT_EQUAL_UINT16(5850, b.humShtCenti);
    TEST_ASSERT_EQUAL_UINT16(17, b.puertaAbiertaS);
    TEST_ASSERT_EQUAL_UINT16(95, b.puertaRachaS);
    TEST_ASSERT_EQUAL_UINT8(a.banderas, b.banderas);
}

void test_registro_con_un_bit_alterado_se_detecta(void) {
    uint8_t buf[core::TAM_REGISTRO];
    core::codificarRegistro(registroEjemplo(), buf);
    for (size_t i = 0; i < core::TAM_REGISTRO; ++i) {
        buf[i] ^= 0x04;
        core::RegistroLectura r;
        TEST_ASSERT_FALSE_MESSAGE(core::decodificarRegistro(buf, r), "byte alterado no detectado");
        buf[i] ^= 0x04;
    }
}

void test_centesimas_redondean_y_nulo_para_no_finitos(void) {
    TEST_ASSERT_EQUAL_INT16(406, core::aCentesimas(4.0625f));
    TEST_ASSERT_EQUAL_INT16(-1013, core::aCentesimas(-10.125f));
    TEST_ASSERT_EQUAL_INT16(core::NULO_I16, core::aCentesimas(NAN));
    TEST_ASSERT_EQUAL_UINT16(core::NULO_U16, core::aCentesimasSinSigno(-1.0f));
    TEST_ASSERT_TRUE(std::isnan(core::deCentesimas(core::NULO_I16)));
}

// ── Payload v1.0 ───────────────────────────────────────────────────────────

void test_payload_ejemplo_del_contrato(void) {
    core::BaseTiempo base;
    const std::string j = core::serializarTelemetria(registroEjemplo(), DEVICE, base);
    TEST_ASSERT_FALSE(j.empty());
    TEST_ASSERT_TRUE(contiene(j, "\"schema_version\":\"1.0\""));
    TEST_ASSERT_TRUE(contiene(j, "\"device_id\":\"ESP32_FARMACIA_01\""));
    TEST_ASSERT_TRUE(contiene(j, "\"boot_id\":42"));
    TEST_ASSERT_TRUE(contiene(j, "\"seq_no\":1205"));
    TEST_ASSERT_TRUE(contiene(j, "\"captured_at\":\"2026-09-25T15:30:00Z\""));
    TEST_ASSERT_TRUE(contiene(j, "\"time_quality\":\"sntp_synced\""));
    TEST_ASSERT_TRUE(contiene(j, "\"temp_ds18b20\":4.25"));
    TEST_ASSERT_TRUE(contiene(j, "\"temp_sht31\":21.10"));
    TEST_ASSERT_TRUE(contiene(j, "\"humedad_sht31\":58.50"));
    TEST_ASSERT_TRUE(contiene(j, "\"door_open\":false"));
    TEST_ASSERT_TRUE(contiene(j, "\"mc38_status\":\"installed\""));
    TEST_ASSERT_TRUE(contiene(j, "\"sensor_fault\":false"));
    TEST_ASSERT_TRUE(contiene(j, "\"firmware_version\":\"v1.2.0\""));
    TEST_ASSERT_FALSE(contiene(j, "received_at"));  // lo agrega el backend
    TEST_ASSERT_FALSE(contiene(j, "simulated"));    // nunca en producción
}

/// HU-01 esc. 2/3 y la regla del proyecto: una falla del DS18B20 sale como
/// null + SENSOR_FAULT, nunca como número (ni 0, ni −127, ni 85).
void test_payload_falla_ds18b20_es_null_con_sensor_fault(void) {
    core::RegistroLectura r = registroEjemplo();
    core::fijarDS18B20(r, core::LecturaSensor::falla(FalloSensor::ErrorCrc));
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"temp_ds18b20\":null"));
    TEST_ASSERT_TRUE(contiene(j, "\"ds18b20_status\":\"SENSOR_FAULT\""));
    TEST_ASSERT_TRUE(contiene(j, "\"ds18b20_fault\":\"crc_error\""));
    TEST_ASSERT_TRUE(contiene(j, "\"sensor_fault\":true"));
    TEST_ASSERT_FALSE(contiene(j, "-127"));
    // HU-02 esc. 2: el SHT31 no se ve afectado.
    TEST_ASSERT_TRUE(contiene(j, "\"temp_sht31\":21.10"));
    TEST_ASSERT_TRUE(contiene(j, "\"sht31_status\":\"OK\""));
}

/// Prueba de aislamiento I²C: SHT31 desconectado, DS18B20 intacto.
void test_payload_falla_sht31_no_invalida_ds18b20(void) {
    core::RegistroLectura r = registroEjemplo();
    core::fijarSHT31(r, core::falloSHT31(FalloSensor::SinRespuesta));
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"temp_ds18b20\":4.25"));
    TEST_ASSERT_TRUE(contiene(j, "\"ds18b20_status\":\"OK\""));
    TEST_ASSERT_TRUE(contiene(j, "\"temp_sht31\":null"));
    TEST_ASSERT_TRUE(contiene(j, "\"humedad_sht31\":null"));
    TEST_ASSERT_TRUE(contiene(j, "\"sht31_fault\":\"no_response\""));
    TEST_ASSERT_TRUE(contiene(j, "\"sensor_fault\":true"));
}

void test_payload_sin_mc38_door_open_null(void) {
    core::RegistroLectura r = registroEjemplo();
    r.poner(core::BANDERA_MC38_INSTALADO, false);
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"door_open\":null"));
    TEST_ASSERT_TRUE(contiene(j, "\"mc38_status\":\"not_installed\""));
    TEST_ASSERT_TRUE(contiene(j, "\"sensor_fault\":false"));  // no es una falla
}

void test_payload_puerta_prolongada_agrega_racha(void) {
    core::RegistroLectura r = registroEjemplo();
    r.poner(core::BANDERA_PUERTA_ABIERTA, true);
    r.puertaAbiertaS = 30;
    r.puertaRachaS = 185;
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"door_open\":true"));
    TEST_ASSERT_TRUE(contiene(j, "\"door_open_s\":30"));
    TEST_ASSERT_TRUE(contiene(j, "\"door_open_streak_s\":185"));
}

void test_payload_incoherente_no_se_publica(void) {
    core::RegistroLectura r = registroEjemplo();
    r.falloDs = FalloSensor::ErrorCrc;  // valor presente pero marcado con falla
    TEST_ASSERT_TRUE(core::serializarTelemetria(r, DEVICE, core::BaseTiempo()).empty());

    r = registroEjemplo();
    TEST_ASSERT_TRUE(core::serializarTelemetria(r, "farmacia/01", core::BaseTiempo()).empty());
    TEST_ASSERT_TRUE(core::serializarTelemetria(r, "", core::BaseTiempo()).empty());

    r.poner(core::BANDERA_MC38_INSTALADO, false);
    r.poner(core::BANDERA_PUERTA_ABIERTA, true);  // puerta sin sensor
    TEST_ASSERT_TRUE(core::serializarTelemetria(r, DEVICE, core::BaseTiempo()).empty());
}

/// HU-05 esc. 3: el peor caso (todos los campos al máximo) cabe en el límite;
/// y si el límite fuera menor, no se publica una estructura truncada.
void test_payload_peor_caso_cabe_y_el_exceso_no_se_publica(void) {
    core::RegistroLectura r = registroEjemplo();
    r.bootId = 4294967295u;
    r.seqNo = 4294967295u;
    r.uptimeS = 4294967295u;
    core::fijarDS18B20(r, core::LecturaSensor::falla(FalloSensor::ValorPowerOn));
    core::fijarSHT31(r, core::falloSHT31(FalloSensor::SinRespuesta));
    r.poner(core::BANDERA_PUERTA_ABIERTA, true);
    r.puertaAbiertaS = 65535;
    r.puertaRachaS = 65535;
    r.poner(core::BANDERA_SENSADO_ENERGIA, true);
    r.poner(core::BANDERA_EN_RESPALDO, true);
    r.poner(core::BANDERA_SIMULADO, true);
    r.fwMayor = r.fwMenor = r.fwParche = 255;
    const std::string largo(50, 'X');
    const std::string j = core::serializarTelemetria(r, largo, core::BaseTiempo());
    TEST_ASSERT_FALSE(j.empty());
    TEST_ASSERT_TRUE(j.size() <= core::PAYLOAD_MAX_BYTES);
    TEST_ASSERT_TRUE(core::serializarTelemetria(r, largo, core::BaseTiempo(), 100).empty());
}

void test_payload_simulado_se_marca(void) {
    core::RegistroLectura r = registroEjemplo();
    r.poner(core::BANDERA_SIMULADO, true);
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"simulated\":true"));
}

// ── Tiempo ─────────────────────────────────────────────────────────────────

void test_iso8601(void) {
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", core::formatearISO8601(EPOCH_EJEMPLO).c_str());
    TEST_ASSERT_EQUAL_STRING("2000-02-29T23:59:59Z", core::formatearISO8601(951868799u).c_str());
    TEST_ASSERT_EQUAL_STRING("2106-02-07T06:28:15Z", core::formatearISO8601(4294967295u).c_str());
}

/// Sin SNTP no se inventa la hora: captured_at = null y time_quality = unsynced.
void test_sin_sntp_captured_at_es_null(void) {
    core::RegistroLectura r = registroEjemplo();
    r.poner(core::BANDERA_TIEMPO_SNTP, false);
    r.capturadoEpoch = 0;
    const std::string j = core::serializarTelemetria(r, DEVICE, core::BaseTiempo());
    TEST_ASSERT_TRUE(contiene(j, "\"captured_at\":null"));
    TEST_ASSERT_TRUE(contiene(j, "\"time_quality\":\"unsynced\""));
    TEST_ASSERT_TRUE(contiene(j, "\"uptime_s\":36000"));
}

/// Lectura sin hora, pero SNTP llegó después en el MISMO arranque: se
/// retrofecha con el uptime monótono y se declara como tal.
void test_retrofechado_en_el_mismo_arranque(void) {
    core::RegistroLectura r = registroEjemplo();
    r.poner(core::BANDERA_TIEMPO_SNTP, false);
    r.capturadoEpoch = 0;
    r.uptimeS = 100;
    core::BaseTiempo base;
    base.fijar(42, EPOCH_EJEMPLO, 400);  // SNTP llegó en uptime 400
    const std::string j = core::serializarTelemetria(r, DEVICE, base);
    TEST_ASSERT_TRUE(contiene(j, "\"captured_at\":\"2026-09-25T15:25:00Z\""));
    TEST_ASSERT_TRUE(contiene(j, "\"time_quality\":\"sntp_backfilled\""));

    base.fijar(43, EPOCH_EJEMPLO, 400);  // otro arranque: el uptime no es comparable
    TEST_ASSERT_TRUE(contiene(core::serializarTelemetria(r, DEVICE, base), "\"captured_at\":null"));
}

void test_evento_y_lwt(void) {
    core::BaseTiempo base;
    base.fijar(42, EPOCH_EJEMPLO, 1000);
    core::ObjetoJSON detalle;
    detalle.sinSigno("pendientes", 24000);
    detalle.sinSigno("ocupacion_pct", 80);
    const std::string e = core::serializarEvento(DEVICE, 42, "buffer_80", 0, 1060, base, detalle.cerrar());
    TEST_ASSERT_TRUE(contiene(e, "\"event_type\":\"buffer_80\""));
    TEST_ASSERT_TRUE(contiene(e, "\"occurred_at\":\"2026-09-25T15:31:00Z\""));
    TEST_ASSERT_TRUE(contiene(e, "\"detail\":{\"pendientes\":24000,\"ocupacion_pct\":80}"));

    // HU-13: el LWT lleva el boot_id de la sesión para descartar LWT atrasados.
    const std::string lwt = core::serializarLWT(DEVICE, 42);
    TEST_ASSERT_EQUAL_STRING(
        "{\"schema_version\":\"1.0\",\"device_id\":\"ESP32_FARMACIA_01\",\"boot_id\":42,"
        "\"event_type\":\"lwt_offline\"}",
        lwt.c_str());
}

void run_tests_contrato(void) {
    RUN_TEST(test_json_nan_e_infinito_salen_como_null);
    RUN_TEST(test_json_escapa_cadenas);
    RUN_TEST(test_json_lee_formato_python_con_espacios);
    RUN_TEST(test_json_no_confunde_claves_anidadas_ni_valores);
    RUN_TEST(test_json_rechaza_numeros_no_uint32);
    RUN_TEST(test_crc32_valor_de_verificacion);
    RUN_TEST(test_registro_ida_y_vuelta);
    RUN_TEST(test_registro_con_un_bit_alterado_se_detecta);
    RUN_TEST(test_centesimas_redondean_y_nulo_para_no_finitos);
    RUN_TEST(test_payload_ejemplo_del_contrato);
    RUN_TEST(test_payload_falla_ds18b20_es_null_con_sensor_fault);
    RUN_TEST(test_payload_falla_sht31_no_invalida_ds18b20);
    RUN_TEST(test_payload_sin_mc38_door_open_null);
    RUN_TEST(test_payload_puerta_prolongada_agrega_racha);
    RUN_TEST(test_payload_incoherente_no_se_publica);
    RUN_TEST(test_payload_peor_caso_cabe_y_el_exceso_no_se_publica);
    RUN_TEST(test_payload_simulado_se_marca);
    RUN_TEST(test_iso8601);
    RUN_TEST(test_sin_sntp_captured_at_es_null);
    RUN_TEST(test_retrofechado_en_el_mismo_arranque);
    RUN_TEST(test_evento_y_lwt);
}
