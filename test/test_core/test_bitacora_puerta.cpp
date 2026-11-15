/**
 * HU-04 esc. 1 / HU-35: los cambios de puerta se guardan (acotados) hasta su
 * PUBACK, sobreviven a un reinicio y se publican con la hora del cambio.
 */
#include <unity.h>

#include <string>
#include <vector>

#include "core/BitacoraPuerta.h"
#include "core/Json.h"
#include "core/Payload.h"

using core::EventoPuerta;
using core::ResultadoAnotacion;

static const uint32_t EPOCH_PT = 1790350200u;  // 2026-09-25T15:30:00Z

/// Archivo de LittleFS en memoria (persiste entre instancias).
class ArchivoMemoria : public core::AlmacenBitacora {
public:
    std::vector<uint8_t> datos;
    bool existe = false;
    bool fallarEscritura = false;
    int escrituras = 0;
    bool leer(std::vector<uint8_t>& d) override {
        if (!existe) return false;
        d = datos;
        return true;
    }
    bool escribir(const std::vector<uint8_t>& d) override {
        if (fallarEscritura) return false;
        datos = d;
        existe = true;
        escrituras++;
        return true;
    }
};

void test_puerta_fifo_y_confirmacion_por_id(void) {
    ArchivoMemoria f;
    core::BitacoraPuerta b(f, 10);
    TEST_ASSERT_TRUE(b.montar());
    TEST_ASSERT_TRUE(b.anotar(true, 4, 100, EPOCH_PT) == ResultadoAnotacion::Guardado);
    TEST_ASSERT_TRUE(b.anotar(false, 4, 130, EPOCH_PT + 30) == ResultadoAnotacion::Guardado);
    TEST_ASSERT_EQUAL_UINT32(2, b.pendientes());
    TEST_ASSERT_EQUAL_INT(2, f.escrituras);  // cada cambio, a flash antes de publicarlo

    EventoPuerta e;
    TEST_ASSERT_TRUE(b.primero(e));
    TEST_ASSERT_TRUE(e.abierta);
    TEST_ASSERT_EQUAL_UINT32(100, e.uptimeS);
    TEST_ASSERT_FALSE(b.confirmar(e.id + 1));  // un id que no es el primero no retira nada
    TEST_ASSERT_EQUAL_UINT32(2, b.pendientes());
    TEST_ASSERT_TRUE(b.confirmar(e.id));
    TEST_ASSERT_TRUE(b.primero(e));
    TEST_ASSERT_FALSE(e.abierta);
    TEST_ASSERT_TRUE(b.confirmar(e.id));
    TEST_ASSERT_FALSE(b.primero(e));
}

/// Lo que se perdía antes: el nodo reinicia (o se corta la luz) con cambios
/// sin publicar. Al montar se recuperan en orden y los ids siguen creciendo.
void test_puerta_los_cambios_sobreviven_al_reinicio(void) {
    ArchivoMemoria f;
    uint32_t idAntes = 0;
    {
        core::BitacoraPuerta b(f, 10);
        b.montar();
        b.anotar(true, 7, 10, 0);   // arranque sin SNTP
        b.anotar(false, 7, 25, 0);
        EventoPuerta e;
        b.primero(e);
        idAntes = e.id;
    }
    core::BitacoraPuerta tras(f, 10);
    TEST_ASSERT_TRUE(tras.montar());
    TEST_ASSERT_EQUAL_UINT32(2, tras.pendientes());
    EventoPuerta e;
    TEST_ASSERT_TRUE(tras.primero(e));
    TEST_ASSERT_EQUAL_UINT32(idAntes, e.id);
    TEST_ASSERT_EQUAL_UINT32(7, e.bootId);
    TEST_ASSERT_TRUE(e.abierta);
    tras.anotar(true, 8, 5, EPOCH_PT);
    tras.confirmar(e.id);
    tras.primero(e);
    TEST_ASSERT_EQUAL_UINT32(25, e.uptimeS);
    tras.confirmar(e.id);
    tras.primero(e);
    TEST_ASSERT_TRUE(e.id > idAntes + 1);  // el nuevo no reutiliza ids
    TEST_ASSERT_EQUAL_UINT32(8, e.bootId);
}

/// Acotada: llena, conserva los existentes y cuenta el descarte (ninguna
/// pérdida silenciosa), también tras reiniciar.
void test_puerta_llena_conserva_los_existentes_y_cuenta_el_descarte(void) {
    ArchivoMemoria f;
    {
        core::BitacoraPuerta b(f, 3);
        b.montar();
        for (uint32_t i = 0; i < 3; ++i) TEST_ASSERT_TRUE(b.anotar(i % 2 == 0, 1, i, 0) == ResultadoAnotacion::Guardado);
        TEST_ASSERT_TRUE(b.anotar(true, 1, 99, 0) == ResultadoAnotacion::Descartado);
        TEST_ASSERT_TRUE(b.anotar(false, 1, 100, 0) == ResultadoAnotacion::Descartado);
        TEST_ASSERT_EQUAL_UINT32(3, b.pendientes());
        TEST_ASSERT_EQUAL_UINT32(2, b.descartados());
        EventoPuerta e;
        b.primero(e);
        TEST_ASSERT_EQUAL_UINT32(0, e.uptimeS);  // el más antiguo sigue ahí
    }
    core::BitacoraPuerta tras(f, 3);
    tras.montar();
    TEST_ASSERT_EQUAL_UINT32(2, tras.descartados());
    TEST_ASSERT_EQUAL_UINT32(3, tras.pendientes());
}

void test_puerta_archivo_corrupto_se_cuenta_y_se_empieza_vacia(void) {
    ArchivoMemoria f;
    {
        core::BitacoraPuerta b(f, 10);
        b.montar();
        b.anotar(true, 1, 1, 0);
    }
    f.datos[f.datos.size() / 2] ^= 0xFF;
    core::BitacoraPuerta tras(f, 10);
    TEST_ASSERT_FALSE(tras.montar());
    TEST_ASSERT_EQUAL_UINT32(1, tras.corrupciones());
    TEST_ASSERT_EQUAL_UINT32(0, tras.pendientes());
}

void test_puerta_sin_flash_el_cambio_queda_en_ram(void) {
    ArchivoMemoria f;
    f.fallarEscritura = true;
    core::BitacoraPuerta b(f, 10);
    b.montar();
    TEST_ASSERT_TRUE(b.anotar(true, 1, 1, 0) == ResultadoAnotacion::SinPersistir);
    EventoPuerta e;
    TEST_ASSERT_TRUE(b.primero(e));  // se publicará igual
}

/// La capacidad configurada cabe en un bloque de LittleFS (4 KB).
void test_puerta_capacidad_cabe_en_un_bloque(void) {
    std::deque<EventoPuerta> llenos(200);
    const std::vector<uint8_t> imagen = core::codificarBitacora(llenos, 201, 0);
    TEST_ASSERT_TRUE(imagen.size() <= 4096);
    std::deque<EventoPuerta> leidos;
    uint32_t sig = 0, desc = 0;
    TEST_ASSERT_TRUE(core::decodificarBitacora(imagen, 200, leidos, sig, desc));
    TEST_ASSERT_EQUAL_UINT32(200, (uint32_t)leidos.size());
    // Un archivo con más entradas que la capacidad no se acepta.
    TEST_ASSERT_FALSE(core::decodificarBitacora(imagen, 199, leidos, sig, desc));
}

/// El evento publicado lleva la hora del CAMBIO (no la de la publicación),
/// con el arranque y el uptime en que ocurrió.
void test_puerta_evento_publicado_con_occurred_at_del_cambio(void) {
    EventoPuerta e;
    e.id = 3;
    e.bootId = 41;
    e.uptimeS = 3600;
    e.epoch = EPOCH_PT;
    e.abierta = true;
    core::BaseTiempo base;
    base.fijar(42, EPOCH_PT + 86400, 100);  // se publica al día siguiente, otro arranque
    std::string j = core::serializarEvento("ESP32_FARMACIA_01", e.bootId, "puerta", e.epoch, e.uptimeS, base,
                                           core::detalleEventoPuerta(e));
    std::string v;
    uint32_t n = 0;
    TEST_ASSERT_TRUE(core::leerCadena(j, "event_type", v));
    TEST_ASSERT_EQUAL_STRING("puerta", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "occurred_at", v));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T15:30:00Z", v.c_str());
    TEST_ASSERT_TRUE(core::leerCadena(j, "time_quality", v));
    TEST_ASSERT_EQUAL_STRING("sntp_synced", v.c_str());
    TEST_ASSERT_TRUE(core::leerSinSigno32(j, "boot_id", n));
    TEST_ASSERT_EQUAL_UINT32(41, n);
    TEST_ASSERT_TRUE(j.find("\"detail\":{\"door_open\":true}") != std::string::npos);

    // Sin SNTP en su arranque y publicado en otro: no se inventa la hora.
    e.epoch = 0;
    j = core::serializarEvento("ESP32_FARMACIA_01", e.bootId, "puerta", e.epoch, e.uptimeS, base,
                               core::detalleEventoPuerta(e));
    TEST_ASSERT_TRUE(core::buscarCampo(j, "occurred_at", v) == core::TipoValorJSON::Nulo);
    TEST_ASSERT_TRUE(core::leerCadena(j, "time_quality", v));
    TEST_ASSERT_EQUAL_STRING("unsynced", v.c_str());

    // Sin SNTP al ocurrir, pero en este mismo arranque ya sincronizado: se
    // retrofecha con el uptime (la misma regla que la telemetría).
    e.bootId = 42;
    e.uptimeS = 50;
    j = core::serializarEvento("ESP32_FARMACIA_01", e.bootId, "puerta", e.epoch, e.uptimeS, base,
                               core::detalleEventoPuerta(e));
    TEST_ASSERT_TRUE(core::leerCadena(j, "time_quality", v));
    TEST_ASSERT_EQUAL_STRING("sntp_backfilled", v.c_str());
}

void run_tests_bitacora_puerta(void) {
    RUN_TEST(test_puerta_fifo_y_confirmacion_por_id);
    RUN_TEST(test_puerta_los_cambios_sobreviven_al_reinicio);
    RUN_TEST(test_puerta_llena_conserva_los_existentes_y_cuenta_el_descarte);
    RUN_TEST(test_puerta_archivo_corrupto_se_cuenta_y_se_empieza_vacia);
    RUN_TEST(test_puerta_sin_flash_el_cambio_queda_en_ram);
    RUN_TEST(test_puerta_capacidad_cabe_en_un_bloque);
    RUN_TEST(test_puerta_evento_publicado_con_occurred_at_del_cambio);
}
