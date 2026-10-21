#include "Pem.h"

namespace core {

static bool caracterBase64(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
           c == '/';
}

bool base64Valido(const std::string& b64, size_t maxLargo) {
    if (b64.empty() || b64.size() > maxLargo || b64.size() % 4 != 0) return false;
    size_t relleno = 0;
    while (relleno < 2 && b64[b64.size() - 1 - relleno] == '=') ++relleno;
    for (size_t i = 0; i < b64.size() - relleno; ++i) {
        if (!caracterBase64(b64[i])) return false;
    }
    return true;
}

std::string pemDesdeBase64(const std::string& etiqueta, const std::string& b64) {
    if (!base64Valido(b64)) return std::string();
    std::string pem = "-----BEGIN " + etiqueta + "-----\n";
    for (size_t i = 0; i < b64.size(); i += 64) {
        pem += b64.substr(i, 64);
        pem += '\n';
    }
    pem += "-----END " + etiqueta + "-----\n";
    return pem;
}

}  // namespace core
