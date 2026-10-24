/**
 * Genera payloads REALES del contrato v1.0 con el mismo código que corre en
 * el ESP32 (src/core/Payload.cpp), para validarlos contra el esquema Pydantic
 * del backend (ColdCare1023/Backend: contrato/validar_contrato.py).
 *
 * Así la "Prueba de Formato JSON" del plan de QA no compara el esquema con un
 * JSON escrito a mano, sino con lo que el firmware emite de verdad.
 *
 * Uso: generar_ejemplos <carpeta_salida>
 */
#include <cstdio>
#include <string>

#include "core/Json.h"
#include "core/Payload.h"
#include "core/Registro.h"

using namespace core;

static const std::string DEVICE = "ESP32_FARMACIA_01";
static const uint32_t EPOCH = 1790350200u;  // 2026-09-25T15:30:00Z

static RegistroLectura base() {
    RegistroLectura r;
    r.bootId = 42;
    r.seqNo = 1205;
    r.capturadoEpoch = EPOCH;
    r.uptimeS = 36000;
    r.poner(BANDERA_TIEMPO_SNTP, true);
    fijarDS18B20(r, LecturaSensor::ok(4.25f));
    LecturaSHT31 s;
    s.temperatura = LecturaSensor::ok(21.10f);
    s.humedad = LecturaSensor::ok(58.5f);
    fijarSHT31(r, s);
    r.poner(BANDERA_MC38_INSTALADO, true);
    r.fwMayor = 1;
    r.fwMenor = 2;
    r.fwParche = 0;
    return r;
}

static bool escribir(const std::string& carpeta, const char* nombre, const std::string& json) {
    const std::string ruta = carpeta + "/" + nombre;
    FILE* f = std::fopen(ruta.c_str(), "wb");
    if (f == nullptr) return false;
    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    std::printf("%-40s %4u B\n", nombre, (unsigned)json.size());
    return !json.empty();
}

int main(int argc, char** argv) {
    const std::string salida = argc > 1 ? argv[1] : ".";
    BaseTiempo sinBase;
    bool ok = true;

    ok &= escribir(salida, "telemetria_normal.json", serializarTelemetria(base(), DEVICE, sinBase));

    RegistroLectura r = base();
    fijarDS18B20(r, LecturaSensor::falla(FalloSensor::ErrorCrc));
    ok &= escribir(salida, "telemetria_ds18b20_crc_error.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    fijarDS18B20(r, LecturaSensor::falla(FalloSensor::SinRespuesta));
    ok &= escribir(salida, "telemetria_ds18b20_desconectado.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    fijarSHT31(r, falloSHT31(FalloSensor::SinRespuesta));
    ok &= escribir(salida, "telemetria_sht31_desconectado.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    r.poner(BANDERA_MC38_INSTALADO, false);
    ok &= escribir(salida, "telemetria_sin_mc38.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    r.poner(BANDERA_PUERTA_ABIERTA, true);
    r.puertaAbiertaS = 30;
    r.puertaRachaS = 185;
    ok &= escribir(salida, "telemetria_puerta_prolongada.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    r.poner(BANDERA_TIEMPO_SNTP, false);
    r.capturadoEpoch = 0;
    ok &= escribir(salida, "telemetria_sin_hora.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    r.poner(BANDERA_TIEMPO_SNTP, false);
    r.capturadoEpoch = 0;
    r.uptimeS = 100;
    BaseTiempo conBase;
    conBase.fijar(42, EPOCH, 400);
    ok &= escribir(salida, "telemetria_retrofechada.json", serializarTelemetria(r, DEVICE, conBase));

    r = base();
    r.poner(BANDERA_SENSADO_ENERGIA, true);
    r.poner(BANDERA_EN_RESPALDO, true);
    ok &= escribir(salida, "telemetria_con_respaldo.json", serializarTelemetria(r, DEVICE, sinBase));

    r = base();
    r.tempDsCenti = 1250;
    r.poner(BANDERA_SIMULADO, true);
    ok &= escribir(salida, "telemetria_demo_excursion_simulada.json",
                   serializarTelemetria(r, "ESP32_DEMO_01", sinBase));

    ObjetoJSON d;
    d.sinSigno("perdidas_total", 3);
    d.sinSigno("perdidas_episodio", 3);
    d.booleano("episodio_abierto", true);
    d.sinSigno("desde_boot_id", 42);
    d.sinSigno("desde_seq_no", 1300);
    d.cadena("desde", formatearISO8601(EPOCH + 3000));
    d.sinSigno("hasta_boot_id", 42);
    d.sinSigno("hasta_seq_no", 1302);
    d.cadena("hasta", formatearISO8601(EPOCH + 3060));
    d.cadena("causa", "buffer_lleno");
    BaseTiempo b2;
    b2.fijar(42, EPOCH, 36000);
    ok &= escribir(salida, "evento_buffer_saturado.json",
                   serializarEvento(DEVICE, 42, "buffer_saturado", EPOCH + 3060, 39060, b2, d.cerrar()));
    ok &= escribir(salida, "evento_lwt_offline.json", serializarLWT(DEVICE, 42));

    return ok ? 0 : 1;
}
