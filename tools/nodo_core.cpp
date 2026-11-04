// Núcleo del nodo virtual: el código REAL de src/core como programa de línea
// de comandos, para probar la cadena completa sin un ESP32.
//
// Produce los bytes exactos que el firmware publicaría y decide, igual que
// MQTTManager::_alRecibir + TransmisorMQTT::_desdeAck, qué haría el nodo con
// cada acuse del backend. La parte de red (MQTT sobre TLS) la pone
// Backend/infra-local/scripts/nodo_virtual.py, que llama a este programa.
//
// Compilar (cualquier compilador C++17), desde la raíz del repo:
//   g++ -std=c++17 -Isrc src/core/*.cpp tools/nodo_core.cpp -o .pio/gen/nodo_core
//   Windows sin MinGW ni Visual Studio: `pip install ziglang` y
//   python -m ziglang c++ -std=c++17 -Isrc src/core/*.cpp tools/nodo_core.cpp -o .pio/gen/nodo_core.exe
//
// Comandos (el JSON sale por stdout, sin salto de línea final):
//   tele    <device> <boot> <seq> <epoch> <uptime_s> <temp_int_centi> [simulado]
//   online  <device> <boot> <epoch> <uptime_s> <pendientes>
//   evento  <device> <boot> <tipo> <epoch> <uptime_s> <detalle_json|->
//   lwt     <device> <boot>
//   ack     <device> <boot> <seq> <archivo_json_publicado>    (el acuse por stdin)
//           → LIBERAR | CUARENTENA | CONSERVAR | IGNORADO <motivo>, más la huella
//   desafio <device> <token> <boot> <epoch> <uptime_s>        (el mensaje por stdin)
//           → evento prueba_posesion firmado, o NO_ES_DESAFIO
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "config.h"
#include "core/Ack.h"
#include "core/ColaLecturas.h"
#include "core/Json.h"
#include "core/Payload.h"
#include "core/Posesion.h"
#include "core/Registro.h"
#include "core/Sha256.h"

using namespace core;

static uint32_t u32(const char* s) { return (uint32_t)std::strtoul(s, nullptr, 10); }

static std::string leerStdin() {
    return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
}

/// Lectura normal de los tres sensores, como la arma la tarea de captura.
static RegistroLectura registro(uint32_t boot, uint32_t seq, uint32_t epoch, uint32_t uptimeS, int32_t tempCenti) {
    RegistroLectura r;
    r.bootId = boot;
    r.seqNo = seq;
    r.capturadoEpoch = epoch;
    r.uptimeS = uptimeS;
    r.poner(BANDERA_TIEMPO_SNTP, true);
    fijarDS18B20(r, LecturaSensor::ok(tempCenti / 100.0f));
    LecturaSHT31 s;
    s.temperatura = LecturaSensor::ok(21.10f);
    s.humedad = LecturaSensor::ok(58.5f);
    fijarSHT31(r, s);
    r.poner(BANDERA_MC38_INSTALADO, true);
    r.fwMayor = FW_MAYOR;
    r.fwMenor = FW_MENOR;
    r.fwParche = FW_PARCHE;
    return r;
}

static const char* decision(ResultadoEnvio r) {
    switch (r) {
        case ResultadoEnvio::Confirmado:  return "LIBERAR";
        case ResultadoEnvio::Rechazado:   return "CUARENTENA";
        case ResultadoEnvio::Invalido:    return "INVALIDO";
        case ResultadoEnvio::Fallo:       return "CONSERVAR";
        case ResultadoEnvio::SinConexion: return "SIN_CONEXION";
    }
    return "?";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: ver la cabecera de tools/nodo_core.cpp\n";
        return 2;
    }
    const std::string cmd = argv[1];
    BaseTiempo base;

    if (cmd == "tele" && argc >= 8) {
        RegistroLectura r = registro(u32(argv[3]), u32(argv[4]), u32(argv[5]), u32(argv[6]), std::atoi(argv[7]));
        if (argc >= 9) r.poner(BANDERA_SIMULADO, true);
        base.fijar(r.bootId, r.capturadoEpoch, r.uptimeS);
        const std::string json = serializarTelemetria(r, argv[2], base);
        if (json.empty()) {
            std::string motivo;
            validarRegistro(r, motivo);
            std::cerr << "registro invalido: " << motivo << "\n";
            return 1;
        }
        std::cout << json;
        return 0;
    }
    if (cmd == "online" && argc >= 7) {
        const uint32_t boot = u32(argv[3]), epoch = u32(argv[4]), uptimeS = u32(argv[5]);
        base.fijar(boot, epoch, uptimeS);
        ObjetoJSON d;  // los mismos campos que publica la tarea de red al conectar
        d.entero("rssi_dbm", -61);
        d.sinSigno("pendientes", u32(argv[6]));
        d.cadena("firmware_version", textoVersionFirmware(FW_MAYOR, FW_MENOR, FW_PARCHE));
        std::cout << serializarEvento(argv[2], boot, "online", epoch, uptimeS, base, d.cerrar());
        return 0;
    }
    if (cmd == "evento" && argc >= 8) {
        const uint32_t boot = u32(argv[3]), epoch = u32(argv[5]), uptimeS = u32(argv[6]);
        base.fijar(boot, epoch, uptimeS);
        const std::string detalle = std::string(argv[7]) == "-" ? "" : argv[7];
        std::cout << serializarEvento(argv[2], boot, argv[4], epoch, uptimeS, base, detalle);
        return 0;
    }
    if (cmd == "lwt" && argc >= 4) {
        std::cout << serializarLWT(argv[2], u32(argv[3]));
        return 0;
    }
    if (cmd == "ack" && argc >= 6) {
        std::ifstream f(argv[5], std::ios::binary);
        const std::string publicado((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        AckLogico ack;
        if (!parsearAck(leerStdin(), ack)) {
            std::cout << "IGNORADO acuse_no_reconocido";
            return 0;
        }
        if (ack.deviceId != argv[2]) {
            std::cout << "IGNORADO device_id_ajeno";
            return 0;
        }
        if (ack.bootId != u32(argv[3]) || ack.seqNo != u32(argv[4])) {
            std::cout << "IGNORADO otra_lectura";
            return 0;
        }
        MemoriaEnvios envios;  // IOT-008: huella anotada antes de publicar
        envios.anotar(ack.bootId, ack.seqNo, sha256Hex(publicado));
        const bool coincide = envios.coincide(ack.bootId, ack.seqNo, ack.contenidoSha256);
        std::cout << decision(resultadoDeAck(ack.estado, coincide)) << (coincide ? " huella_ok" : " huella_ajena");
        return 0;
    }
    if (cmd == "desafio" && argc >= 7) {
        std::string nonce;
        if (!parsearDesafioPosesion(leerStdin(), argv[2], nonce)) {
            std::cout << "NO_ES_DESAFIO";
            return 0;
        }
        const uint32_t boot = u32(argv[4]), epoch = u32(argv[5]), uptimeS = u32(argv[6]);
        base.fijar(boot, epoch, uptimeS);
        ObjetoJSON d;
        d.cadena("nonce", nonce);
        d.cadena("hmac", respuestaPosesion(argv[3], argv[2], nonce));
        std::cout << serializarEvento(argv[2], boot, "prueba_posesion", epoch, uptimeS, base, d.cerrar());
        return 0;
    }
    std::cerr << "comando o argumentos invalidos: ver la cabecera de tools/nodo_core.cpp\n";
    return 2;
}
