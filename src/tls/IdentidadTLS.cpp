#include "IdentidadTLS.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

#include <cstring>
#include <vector>

namespace tls {

namespace {

/// DRBG sembrado con la entropía de la plataforma; se libera al salir.
class Aleatorio {
public:
    Aleatorio() {
        mbedtls_entropy_init(&_entropia);
        mbedtls_ctr_drbg_init(&_drbg);
        static const char kPersonalizacion[] = "thermotrace-identidad-tls";
        _ok = mbedtls_ctr_drbg_seed(&_drbg, mbedtls_entropy_func, &_entropia,
                                    reinterpret_cast<const unsigned char*>(kPersonalizacion),
                                    sizeof(kPersonalizacion) - 1) == 0;
    }
    ~Aleatorio() {
        mbedtls_ctr_drbg_free(&_drbg);
        mbedtls_entropy_free(&_entropia);
    }
    Aleatorio(const Aleatorio&) = delete;
    Aleatorio& operator=(const Aleatorio&) = delete;

    bool ok() const { return _ok; }
    mbedtls_ctr_drbg_context* drbg() { return &_drbg; }

private:
    mbedtls_entropy_context _entropia;
    mbedtls_ctr_drbg_context _drbg;
    bool _ok = false;
};

/// Clave privada leída de un PEM; se libera (y borra) al salir.
class Clave {
public:
    Clave() { mbedtls_pk_init(&_pk); }
    ~Clave() { mbedtls_pk_free(&_pk); }
    Clave(const Clave&) = delete;
    Clave& operator=(const Clave&) = delete;

    bool leer(const std::string& pem, Aleatorio& rng) {
        // mbedtls exige el terminador NUL incluido en el largo de un PEM.
        return mbedtls_pk_parse_key(&_pk, reinterpret_cast<const unsigned char*>(pem.c_str()),
                                    pem.size() + 1, nullptr, 0, mbedtls_ctr_drbg_random,
                                    rng.drbg()) == 0;
    }
    mbedtls_pk_context* pk() { return &_pk; }

private:
    mbedtls_pk_context _pk;
};

std::string textoDe(std::vector<unsigned char>& buffer) {
    std::string texto(reinterpret_cast<const char*>(buffer.data()));
    mbedtls_platform_zeroize(buffer.data(), buffer.size());
    return texto;
}

}  // namespace

bool generarClavePem(std::string& clavePem) {
    Aleatorio rng;
    if (!rng.ok()) return false;
    Clave clave;
    if (mbedtls_pk_setup(clave.pk(), mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) return false;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(*clave.pk()), mbedtls_ctr_drbg_random,
                            rng.drbg()) != 0) {
        return false;
    }
    std::vector<unsigned char> pem(512, 0);
    if (mbedtls_pk_write_key_pem(clave.pk(), pem.data(), pem.size()) != 0) {
        mbedtls_platform_zeroize(pem.data(), pem.size());
        return false;
    }
    clavePem = textoDe(pem);
    return true;
}

bool generarCsrPem(const std::string& clavePem, const std::string& deviceId, std::string& csrPem) {
    Aleatorio rng;
    Clave clave;
    if (!rng.ok() || !clave.leer(clavePem, rng)) return false;

    mbedtls_x509write_csr csr;
    mbedtls_x509write_csr_init(&csr);
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    mbedtls_x509write_csr_set_key(&csr, clave.pk());
    const std::string sujeto = "CN=" + deviceId;
    std::vector<unsigned char> pem(1024, 0);
    const bool ok = mbedtls_x509write_csr_set_subject_name(&csr, sujeto.c_str()) == 0 &&
                    mbedtls_x509write_csr_pem(&csr, pem.data(), pem.size(), mbedtls_ctr_drbg_random,
                                              rng.drbg()) == 0;
    mbedtls_x509write_csr_free(&csr);
    if (ok) csrPem = textoDe(pem);
    return ok;
}

const char* nombreResultado(ResultadoCertificado r) {
    switch (r) {
        case ResultadoCertificado::Valido:          return "valido";
        case ResultadoCertificado::NoEsCertificado: return "no_es_certificado";
        case ResultadoCertificado::ClaveInvalida:   return "clave_invalida";
        case ResultadoCertificado::ClaveNoCoincide: return "clave_no_coincide";
        case ResultadoCertificado::OtroNodo:        return "otro_nodo";
        default:                                    return "no_es_de_cliente";
    }
}

ResultadoCertificado validarCertificado(const std::string& certPem, const std::string& clavePem,
                                        const std::string& deviceId) {
    Aleatorio rng;
    Clave clave;
    if (!rng.ok() || !clave.leer(clavePem, rng)) return ResultadoCertificado::ClaveInvalida;

    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    struct Liberar {
        mbedtls_x509_crt* c;
        ~Liberar() { mbedtls_x509_crt_free(c); }
    } liberar{&crt};

    if (mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(certPem.c_str()),
                               certPem.size() + 1) != 0 ||
        crt.next != nullptr) {
        return ResultadoCertificado::NoEsCertificado;  // ninguno, o una cadena en vez de uno
    }
    if (mbedtls_pk_check_pair(&crt.pk, clave.pk(), mbedtls_ctr_drbg_random, rng.drbg()) != 0) {
        return ResultadoCertificado::ClaveNoCoincide;
    }

    // Exactamente un CN y que sea este device_id: el broker lo usa como identidad.
    int cns = 0;
    bool coincide = false;
    for (const mbedtls_x509_name* n = &crt.subject; n != nullptr; n = n->next) {
        if (n->oid.p == nullptr || MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) != 0) continue;
        ++cns;
        coincide = n->val.len == deviceId.size() &&
                   std::memcmp(n->val.p, deviceId.data(), deviceId.size()) == 0;
    }
    if (cns != 1 || !coincide) return ResultadoCertificado::OtroNodo;

    if (mbedtls_x509_crt_check_extended_key_usage(&crt, MBEDTLS_OID_CLIENT_AUTH,
                                                  MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH)) != 0) {
        return ResultadoCertificado::NoEsDeCliente;
    }
    return ResultadoCertificado::Valido;
}

}  // namespace tls
