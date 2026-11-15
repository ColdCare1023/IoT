/**
 * H1 del banco (HU-08 esc. 3, HU-09 esc. 3): plazos de la conexión al broker
 * en la unidad que espera cada API de Arduino-ESP32 3.x, y watchdog que cubre
 * el peor intento de conexión sin pasar del keep-alive.
 *
 * La última prueba es una revisión estática: lee src/network/MQTTManager.cpp
 * y exige que cada `setTimeout`/`setHandshakeTimeout` reciba la constante con
 * la unidad en el nombre. El defecto original era exactamente
 * `_tls.setTimeout(TLS_HANDSHAKE_TIMEOUT_MS / 1000)` (10 ms de plazo TCP).
 */
#include <unity.h>

#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "core/PlazosConexion.h"

void test_plazo_del_cliente_tls_va_en_milisegundos(void) {
    // Stream::setTimeout(ms): 10 s, no 10 ms.
    TEST_ASSERT_EQUAL_UINT32(10000, core::PLAZO_CLIENTE_TLS_MS);
    TEST_ASSERT_EQUAL_UINT32(TLS_HANDSHAKE_TIMEOUT_MS, core::PLAZO_CLIENTE_TLS_MS);
}

void test_plazo_del_handshake_va_en_segundos(void) {
    // setHandshakeTimeout(s) lo multiplica por 1000 dentro del core.
    TEST_ASSERT_EQUAL_UINT32(10, core::PLAZO_HANDSHAKE_TLS_S);
    TEST_ASSERT_EQUAL_UINT32(5000, core::PLAZO_COMANDO_MQTT_MS);
}

void test_espera_dns_de_lwip_por_servidor_y_total(void) {
    // dns.c: 1 s al enviar y luego tmr = retries → 1 + 1 + 2 + 3 = 7 s.
    TEST_ASSERT_EQUAL_UINT32(1000, core::esperaDnsPorServidorMs(1));
    TEST_ASSERT_EQUAL_UINT32(2000, core::esperaDnsPorServidorMs(2));
    TEST_ASSERT_EQUAL_UINT32(4000, core::esperaDnsPorServidorMs(3));
    TEST_ASSERT_EQUAL_UINT32(7000, core::esperaDnsPorServidorMs(4));
    TEST_ASSERT_EQUAL_UINT32(21000, core::DNS_PEOR_BLOQUEO_DERIVADO_MS);
    TEST_ASSERT_TRUE(DNS_PEOR_BLOQUEO_MS >= core::DNS_PEOR_BLOQUEO_DERIVADO_MS);
}

void test_watchdog_cubre_el_peor_intento_y_no_pasa_el_keepalive(void) {
    // DNS 21 s + TCP 10 s + TLS (10 s + una lectura de 10 s) + CONNACK 5 s.
    TEST_ASSERT_EQUAL_UINT32(56000, core::PEOR_BLOQUEO_CONEXION_MS);
    TEST_ASSERT_TRUE(core::WATCHDOG_PLAZO_S * 1000u > core::PEOR_BLOQUEO_CONEXION_MS);
    TEST_ASSERT_TRUE(core::WATCHDOG_PLAZO_S <= MQTT_KEEPALIVE_S);
    TEST_ASSERT_TRUE(ACK_LOGICO_TIMEOUT_MS < core::WATCHDOG_PLAZO_S * 1000u);
}

// ── Revisión estática de MQTTManager.cpp ──────────────────────────────────

static std::string leerArchivo(const std::string& ruta) {
    std::ifstream f(ruta.c_str(), std::ios::binary);
    if (!f) return std::string();
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

/// Ruta del fuente a partir de la de esta prueba (__FILE__), con respaldo
/// relativo al directorio de trabajo (pio test corre en la raíz del proyecto).
static std::string fuenteMQTTManager() {
    const std::string esta = __FILE__;
    std::vector<std::string> candidatos;
    for (const char* marca : {"test/test_core/", "test\\test_core\\"}) {
        const size_t corte = esta.rfind(marca);
        if (corte != std::string::npos) candidatos.push_back(esta.substr(0, corte) + "src/network/MQTTManager.cpp");
    }
    candidatos.push_back("src/network/MQTTManager.cpp");
    candidatos.push_back("../src/network/MQTTManager.cpp");
    for (const std::string& ruta : candidatos) {
        const std::string contenido = leerArchivo(ruta);
        if (!contenido.empty()) return contenido;
    }
    return std::string();
}

/// Quita comentarios // y /* */ (respetando literales de cadena).
static std::string sinComentarios(const std::string& s) {
    std::string r;
    bool cadena = false, linea = false, bloque = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char sig = i + 1 < s.size() ? s[i + 1] : '\0';
        if (linea) {
            if (c == '\n') {
                linea = false;
                r += c;
            }
            continue;
        }
        if (bloque) {
            if (c == '*' && sig == '/') {
                bloque = false;
                ++i;
            }
            continue;
        }
        if (cadena) {
            r += c;
            if (c == '\\' && sig != '\0') {
                r += sig;
                ++i;
            } else if (c == '"') {
                cadena = false;
            }
            continue;
        }
        if (c == '"') cadena = true;
        if (c == '/' && sig == '/') {
            linea = true;
            continue;
        }
        if (c == '/' && sig == '*') {
            bloque = true;
            ++i;
            continue;
        }
        r += c;
    }
    return r;
}

struct Llamada {
    std::string receptor;
    std::string argumento;
};

/// Todas las llamadas `<receptor>.<metodo>(<arg>)` del código.
static std::vector<Llamada> llamadas(const std::string& codigo, const std::string& metodo) {
    std::vector<Llamada> r;
    const std::string patron = "." + metodo + "(";
    size_t pos = 0;
    while ((pos = codigo.find(patron, pos)) != std::string::npos) {
        size_t ini = pos;
        while (ini > 0 && (isalnum((unsigned char)codigo[ini - 1]) || codigo[ini - 1] == '_')) --ini;
        Llamada l;
        l.receptor = codigo.substr(ini, pos - ini);
        size_t a = pos + patron.size();
        int profundidad = 1;
        size_t b = a;
        while (b < codigo.size() && profundidad > 0) {
            if (codigo[b] == '(') profundidad++;
            if (codigo[b] == ')') profundidad--;
            if (profundidad > 0) ++b;
        }
        std::string arg = codigo.substr(a, b - a);
        std::string limpio;
        for (char c : arg) {
            if (!isspace((unsigned char)c)) limpio += c;
        }
        l.argumento = limpio;
        r.push_back(l);
        pos = b;
    }
    return r;
}

void test_mqtt_manager_pasa_cada_plazo_en_su_unidad(void) {
    const std::string fuente = fuenteMQTTManager();
    TEST_ASSERT_FALSE_MESSAGE(fuente.empty(), "No se encontro src/network/MQTTManager.cpp para revisarlo");
    const std::string codigo = sinComentarios(fuente);

    const std::vector<Llamada> plazos = llamadas(codigo, "setTimeout");
    const std::vector<Llamada> handshakes = llamadas(codigo, "setHandshakeTimeout");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)plazos.size(), "se esperan _tls.setTimeout y _mqtt.setTimeout");
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)handshakes.size());

    bool tls = false, mqtt = false;
    for (const Llamada& l : plazos) {
        TEST_ASSERT_TRUE_MESSAGE(l.argumento.find("/1000") == std::string::npos,
                                 "H1: setTimeout recibe milisegundos; no se divide por 1000");
        if (l.receptor == "_tls") {
            TEST_ASSERT_EQUAL_STRING("core::PLAZO_CLIENTE_TLS_MS", l.argumento.c_str());
            tls = true;
        } else if (l.receptor == "_mqtt") {
            TEST_ASSERT_EQUAL_STRING("core::PLAZO_COMANDO_MQTT_MS", l.argumento.c_str());
            mqtt = true;
        }
    }
    TEST_ASSERT_TRUE(tls);
    TEST_ASSERT_TRUE(mqtt);
    TEST_ASSERT_EQUAL_STRING("_tls", handshakes[0].receptor.c_str());
    TEST_ASSERT_EQUAL_STRING("core::PLAZO_HANDSHAKE_TLS_S", handshakes[0].argumento.c_str());
}

/// La propia revisión detecta el defecto original (para que no pase en vacío).
void test_la_revision_detecta_el_plazo_en_segundos(void) {
    const std::string defectuoso =
        "void f(){ _tls.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_MS / 1000);\n"
        "  _tls.setTimeout(TLS_HANDSHAKE_TIMEOUT_MS / 1000); // comentario setTimeout(x)\n"
        "  _mqtt.setTimeout(MQTT_COMMAND_TIMEOUT_MS); }";
    const std::vector<Llamada> plazos = llamadas(sinComentarios(defectuoso), "setTimeout");
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)plazos.size());
    TEST_ASSERT_EQUAL_STRING("_tls", plazos[0].receptor.c_str());
    TEST_ASSERT_TRUE(plazos[0].argumento.find("/1000") != std::string::npos);
}

void run_tests_plazos_conexion(void) {
    RUN_TEST(test_plazo_del_cliente_tls_va_en_milisegundos);
    RUN_TEST(test_plazo_del_handshake_va_en_segundos);
    RUN_TEST(test_espera_dns_de_lwip_por_servidor_y_total);
    RUN_TEST(test_watchdog_cubre_el_peor_intento_y_no_pasa_el_keepalive);
    RUN_TEST(test_mqtt_manager_pasa_cada_plazo_en_su_unidad);
    RUN_TEST(test_la_revision_detecta_el_plazo_en_segundos);
}
