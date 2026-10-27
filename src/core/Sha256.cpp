#include "Sha256.h"

#include <cstring>

namespace core {
namespace {

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void procesarBloque(uint32_t h[8], const uint8_t bloque[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t)bloque[4 * i] << 24 | (uint32_t)bloque[4 * i + 1] << 16 |
               (uint32_t)bloque[4 * i + 2] << 8 | (uint32_t)bloque[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

}  // namespace

Sha256::Sha256() { _reiniciar(); }

void Sha256::_reiniciar() {
    static const uint32_t INICIAL[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(_h, INICIAL, sizeof(_h));
    _enBloque = 0;
    _total = 0;
}

void Sha256::actualizar(const uint8_t* datos, size_t largo) {
    _total += largo;
    while (largo > 0) {
        const size_t tomar = (64 - _enBloque) < largo ? (64 - _enBloque) : largo;
        std::memcpy(_bloque + _enBloque, datos, tomar);
        _enBloque += tomar;
        datos += tomar;
        largo -= tomar;
        if (_enBloque == 64) {
            procesarBloque(_h, _bloque);
            _enBloque = 0;
        }
    }
}

std::string Sha256::finalizarHex() {
    uint8_t huella[32];
    finalizar(huella);
    static const char hex[] = "0123456789abcdef";
    std::string salida(64, '0');
    for (int i = 0; i < 32; ++i) {
        salida[2 * i] = hex[huella[i] >> 4];
        salida[2 * i + 1] = hex[huella[i] & 0x0f];
    }
    return salida;
}

void Sha256::finalizar(uint8_t salida[32]) {
    // Relleno: 0x80, ceros y la longitud en bits (big-endian, 64 bits).
    const uint64_t bits = _total * 8u;
    uint8_t cola[128] = {0};
    std::memcpy(cola, _bloque, _enBloque);
    cola[_enBloque] = 0x80;
    const size_t total = (_enBloque + 1 + 8 <= 64) ? 64 : 128;
    for (int k = 0; k < 8; ++k) cola[total - 1 - k] = (uint8_t)(bits >> (8 * k));
    procesarBloque(_h, cola);
    if (total == 128) procesarBloque(_h, cola + 64);

    for (int j = 0; j < 8; ++j) {
        for (int k = 0; k < 4; ++k) salida[4 * j + k] = (uint8_t)(_h[j] >> (24 - 8 * k));
    }
    _reiniciar();
}

std::string sha256Hex(const uint8_t* datos, size_t largo) {
    Sha256 calculo;
    calculo.actualizar(datos, largo);
    return calculo.finalizarHex();
}

std::string sha256Hex(const std::string& texto) {
    return sha256Hex(reinterpret_cast<const uint8_t*>(texto.data()), texto.size());
}

bool igualesTiempoConstante(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diferencia = 0;
    for (size_t i = 0; i < a.size(); ++i) diferencia |= (uint8_t)(a[i] ^ b[i]);
    return diferencia == 0;
}

}  // namespace core
