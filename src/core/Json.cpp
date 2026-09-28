#include "Json.h"

#include <cmath>
#include <cstdio>

namespace core {

// =========================================================================
// Escritura
// =========================================================================

std::string escaparJSON(const std::string& entrada) {
    std::string salida;
    salida.reserve(entrada.size() + 8);
    for (char c : entrada) {
        switch (c) {
            case '"':  salida += "\\\""; break;
            case '\\': salida += "\\\\"; break;
            case '\n': salida += "\\n";  break;
            case '\r': salida += "\\r";  break;
            case '\t': salida += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
                    salida += buf;
                } else {
                    salida += c;
                }
        }
    }
    return salida;
}

ObjetoJSON::ObjetoJSON() {
    _texto.reserve(512);
    _texto += '{';
}

void ObjetoJSON::_clave(const char* clave) {
    if (!_vacio) _texto += ',';
    _vacio = false;
    _texto += '"';
    _texto += escaparJSON(clave != nullptr ? clave : "");
    _texto += "\":";
}

void ObjetoJSON::cadena(const char* clave, const std::string& valor) {
    _clave(clave);
    _texto += '"';
    _texto += escaparJSON(valor);
    _texto += '"';
}

void ObjetoJSON::entero(const char* clave, int64_t valor) {
    _clave(clave);
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", (long long)valor);
    _texto += buf;
}

void ObjetoJSON::sinSigno(const char* clave, uint64_t valor) {
    _clave(clave);
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)valor);
    _texto += buf;
}

void ObjetoJSON::real(const char* clave, double valor, int decimales) {
    _clave(clave);
    if (std::isnan(valor) || std::isinf(valor)) {
        _texto += "null";
        return;
    }
    if (decimales < 0) decimales = 0;
    if (decimales > 6) decimales = 6;
    char buf[40];
    snprintf(buf, sizeof(buf), "%.*f", decimales, valor);
    // "-0.00" es JSON válido pero confunde al leer el log de un refrigerador.
    std::string texto(buf);
    if (texto[0] == '-') {
        bool soloCeros = true;
        for (size_t i = 1; i < texto.size(); ++i) {
            if (texto[i] != '0' && texto[i] != '.') { soloCeros = false; break; }
        }
        if (soloCeros) texto.erase(0, 1);
    }
    _texto += texto;
}

void ObjetoJSON::booleano(const char* clave, bool valor) {
    _clave(clave);
    _texto += valor ? "true" : "false";
}

void ObjetoJSON::nulo(const char* clave) {
    _clave(clave);
    _texto += "null";
}

void ObjetoJSON::crudo(const char* clave, const std::string& fragmento) {
    _clave(clave);
    _texto += fragmento.empty() ? std::string("null") : fragmento;
}

const std::string& ObjetoJSON::cerrar() {
    if (!_cerrado) {
        _texto += '}';
        _cerrado = true;
    }
    return _texto;
}

// =========================================================================
// Lectura (solo primer nivel)
// =========================================================================

static const size_t NPOS = std::string::npos;

static bool esEspacio(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static size_t saltarEspacios(const std::string& s, size_t i) {
    while (i < s.size() && esEspacio(s[i])) ++i;
    return i;
}

/// `s[i]` debe ser la comilla de apertura. Devuelve el índice posterior a la
/// comilla de cierre, o NPOS si la cadena no termina.
static size_t leerCadenaJSON(const std::string& s, size_t i, std::string* salida) {
    ++i;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\') {
            if (i + 1 >= s.size()) return NPOS;
            const char e = s[i + 1];
            if (salida != nullptr) {
                switch (e) {
                    case 'n': *salida += '\n'; break;
                    case 'r': *salida += '\r'; break;
                    case 't': *salida += '\t'; break;
                    case 'b': *salida += '\b'; break;
                    case 'f': *salida += '\f'; break;
                    case 'u': *salida += "\\u"; break;  // se conserva literal
                    default:  *salida += e;    break;   // \" \\ \/
                }
            }
            i += 2;
            continue;
        }
        if (c == '"') return i + 1;
        if (salida != nullptr) *salida += c;
        ++i;
    }
    return NPOS;
}

/// Salta un valor de cualquier tipo empezando en `i`.
static size_t saltarValor(const std::string& s, size_t i) {
    if (i >= s.size()) return NPOS;
    const char c = s[i];
    if (c == '"') return leerCadenaJSON(s, i, nullptr);
    if (c == '{' || c == '[') {
        int profundidad = 0;
        while (i < s.size()) {
            const char d = s[i];
            if (d == '"') {
                i = leerCadenaJSON(s, i, nullptr);
                if (i == NPOS) return NPOS;
                continue;
            }
            if (d == '{' || d == '[') {
                profundidad++;
            } else if (d == '}' || d == ']') {
                profundidad--;
                if (profundidad == 0) return i + 1;
            }
            ++i;
        }
        return NPOS;
    }
    size_t j = i;
    while (j < s.size() && s[j] != ',' && s[j] != '}' && s[j] != ']' && !esEspacio(s[j])) ++j;
    return j == i ? NPOS : j;
}

TipoValorJSON buscarCampo(const std::string& json, const std::string& campo, std::string& valor) {
    size_t i = saltarEspacios(json, 0);
    if (i >= json.size() || json[i] != '{') return TipoValorJSON::NoEncontrado;
    ++i;

    for (;;) {
        i = saltarEspacios(json, i);
        if (i >= json.size() || json[i] != '"') return TipoValorJSON::NoEncontrado;

        std::string clave;
        i = leerCadenaJSON(json, i, &clave);
        if (i == NPOS) return TipoValorJSON::NoEncontrado;

        i = saltarEspacios(json, i);
        if (i >= json.size() || json[i] != ':') return TipoValorJSON::NoEncontrado;
        i = saltarEspacios(json, i + 1);

        const size_t inicio = i;
        const size_t fin = saltarValor(json, i);
        if (fin == NPOS) return TipoValorJSON::NoEncontrado;

        if (clave == campo) {
            const char c = json[inicio];
            if (c == '"') {
                valor.clear();
                leerCadenaJSON(json, inicio, &valor);
                return TipoValorJSON::Cadena;
            }
            valor = json.substr(inicio, fin - inicio);
            if (c == '{' || c == '[') return TipoValorJSON::Compuesto;
            if (valor == "true" || valor == "false") return TipoValorJSON::Booleano;
            if (valor == "null") return TipoValorJSON::Nulo;
            return TipoValorJSON::Numero;
        }

        i = saltarEspacios(json, fin);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        return TipoValorJSON::NoEncontrado;
    }
}

bool leerCadena(const std::string& json, const std::string& campo, std::string& salida) {
    std::string valor;
    if (buscarCampo(json, campo, valor) != TipoValorJSON::Cadena) return false;
    salida = valor;
    return true;
}

bool leerSinSigno32(const std::string& json, const std::string& campo, uint32_t& salida) {
    std::string valor;
    if (buscarCampo(json, campo, valor) != TipoValorJSON::Numero) return false;
    if (valor.empty() || valor.size() > 10) return false;
    uint64_t acumulado = 0;
    for (char c : valor) {
        if (c < '0' || c > '9') return false;  // sin signo, sin decimales
        acumulado = acumulado * 10 + (uint64_t)(c - '0');
    }
    if (acumulado > 0xFFFFFFFFull) return false;
    salida = (uint32_t)acumulado;
    return true;
}

}  // namespace core
