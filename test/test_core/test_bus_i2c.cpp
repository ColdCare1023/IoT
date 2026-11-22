/**
 * HU-02: recuperación del SHT31 tras fallas de comunicación seguidas
 * (liberación del bus I²C con hasta 9 pulsos de SCL + STOP, y reinicio).
 */
#include <unity.h>

#include <string>
#include <vector>

#include "core/BusI2C.h"

using core::FalloSensor;
using core::LecturaSensor;
using core::LecturaSHT31;

static LecturaSHT31 sht(FalloSensor ft, FalloSensor fh) {
    LecturaSHT31 l;
    l.temperatura = ft == FalloSensor::Ninguno ? LecturaSensor::ok(22.0f) : LecturaSensor::falla(ft);
    l.humedad = fh == FalloSensor::Ninguno ? LecturaSensor::ok(55.0f) : LecturaSensor::falla(fh);
    return l;
}

void test_i2c_solo_las_fallas_de_comunicacion_cuentan(void) {
    TEST_ASSERT_TRUE(core::falloDeComunicacion(core::falloSHT31(FalloSensor::SinRespuesta)));
    TEST_ASSERT_TRUE(core::falloDeComunicacion(core::falloSHT31(FalloSensor::Timeout)));
    TEST_ASSERT_TRUE(core::falloDeComunicacion(sht(FalloSensor::ErrorCrc, FalloSensor::ErrorCrc)));
    // El bus responde: no es un bus colgado.
    TEST_ASSERT_FALSE(core::falloDeComunicacion(sht(FalloSensor::Ninguno, FalloSensor::Ninguno)));
    TEST_ASSERT_FALSE(core::falloDeComunicacion(sht(FalloSensor::Ninguno, FalloSensor::ErrorCrc)));
    TEST_ASSERT_FALSE(core::falloDeComunicacion(sht(FalloSensor::Implausible, FalloSensor::Implausible)));
}

void test_i2c_tras_n_fallas_seguidas_toca_recuperar_y_el_contador_vuelve_a_cero(void) {
    core::VigilanteFallasI2C v(3);
    const LecturaSHT31 mudo = core::falloSHT31(FalloSensor::SinRespuesta);
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_TRUE(v.registrar(mudo));  // 3.ª seguida: liberar y reiniciar
    TEST_ASSERT_EQUAL_UINT32(1, v.recuperaciones());
    TEST_ASSERT_EQUAL_UINT32(0, v.consecutivas());
    // La siguiente recuperación exige otras 3 seguidas (no una por medición).
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_TRUE(v.registrar(mudo));
    TEST_ASSERT_EQUAL_UINT32(2, v.recuperaciones());
}

void test_i2c_una_lectura_buena_reinicia_la_cuenta(void) {
    core::VigilanteFallasI2C v(3);
    const LecturaSHT31 mudo = core::falloSHT31(FalloSensor::Timeout);
    v.registrar(mudo);
    v.registrar(mudo);
    TEST_ASSERT_FALSE(v.registrar(sht(FalloSensor::Ninguno, FalloSensor::Ninguno)));
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_FALSE(v.registrar(mudo));
    TEST_ASSERT_EQUAL_UINT32(0, v.recuperaciones());
    // Una implausible también corta la racha: el bus funciona.
    TEST_ASSERT_FALSE(v.registrar(sht(FalloSensor::Implausible, FalloSensor::Ninguno)));
    TEST_ASSERT_EQUAL_UINT32(0, v.consecutivas());
}

/// Bus simulado: un esclavo retiene SDA en bajo hasta ver `soltarTras` flancos
/// de subida de SCL. Registra la secuencia de líneas.
class BusSimulado : public core::PinesBusI2C {
public:
    explicit BusSimulado(int soltarTras) : _soltarTras(soltarTras) {}
    void scl(bool alto) override {
        if (alto && !_scl) _flancos++;
        _scl = alto;
        _anotar();
    }
    void sda(bool alto) override {
        const bool antes = _lineaSda();
        _sdaMaestro = alto;
        if (!antes && _lineaSda() && _scl) _stop = true;  // el maestro sube SDA con SCL alto
        _anotar();
    }
    bool leerSda() override { return _lineaSda(); }
    void esperarMedioCiclo() override {}

    int flancos() const { return _flancos; }
    bool huboStop() const { return _stop; }
    const std::vector<std::string>& traza() const { return _traza; }

private:
    bool _esclavoRetiene() const { return _soltarTras < 0 || _flancos < _soltarTras; }
    bool _lineaSda() const { return _sdaMaestro && !_esclavoRetiene(); }
    void _anotar() { _traza.push_back(std::string(_scl ? "C" : "c") + (_lineaSda() ? "D" : "d")); }

    int _soltarTras;
    int _flancos = 0;
    bool _scl = true;
    bool _sdaMaestro = true;
    bool _stop = false;
    std::vector<std::string> _traza;
};

void test_i2c_liberacion_pulsa_hasta_que_el_esclavo_suelta_y_cierra_con_stop(void) {
    BusSimulado bus(4);  // el esclavo suelta SDA tras 4 pulsos
    const core::ResultadoLiberacionI2C r = core::liberarBusI2C(bus);
    TEST_ASSERT_EQUAL_UINT8(4, r.pulsos);
    TEST_ASSERT_TRUE(r.sdaLibre);
    TEST_ASSERT_TRUE(bus.huboStop());
    // Termina con SCL alto y SDA subiendo después: condición STOP.
    const std::vector<std::string>& t = bus.traza();
    TEST_ASSERT_EQUAL_STRING("CD", t.back().c_str());
    TEST_ASSERT_EQUAL_STRING("Cd", t[t.size() - 2].c_str());
}

void test_i2c_liberacion_no_pasa_de_nueve_pulsos(void) {
    BusSimulado bus(-1);  // SDA en bajo para siempre (p. ej. corto a GND)
    const core::ResultadoLiberacionI2C r = core::liberarBusI2C(bus);
    TEST_ASSERT_EQUAL_UINT8(core::PULSOS_LIBERACION_I2C, r.pulsos);
    TEST_ASSERT_EQUAL_INT(9, bus.flancos() - 1);  // + la subida del STOP
    TEST_ASSERT_FALSE(r.sdaLibre);
}

void test_i2c_bus_libre_no_pulsa_pero_igual_emite_stop(void) {
    BusSimulado bus(0);
    const core::ResultadoLiberacionI2C r = core::liberarBusI2C(bus);
    TEST_ASSERT_EQUAL_UINT8(0, r.pulsos);
    TEST_ASSERT_TRUE(r.sdaLibre);
    TEST_ASSERT_TRUE(bus.huboStop());
}

void run_tests_bus_i2c(void) {
    RUN_TEST(test_i2c_solo_las_fallas_de_comunicacion_cuentan);
    RUN_TEST(test_i2c_tras_n_fallas_seguidas_toca_recuperar_y_el_contador_vuelve_a_cero);
    RUN_TEST(test_i2c_una_lectura_buena_reinicia_la_cuenta);
    RUN_TEST(test_i2c_liberacion_pulsa_hasta_que_el_esclavo_suelta_y_cierra_con_stop);
    RUN_TEST(test_i2c_liberacion_no_pasa_de_nueve_pulsos);
    RUN_TEST(test_i2c_bus_libre_no_pulsa_pero_igual_emite_stop);
}
