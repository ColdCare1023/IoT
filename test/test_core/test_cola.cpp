/**
 * Buffer offline FIFO (HU-06), sincronización con acuse lógico (HU-07) y
 * brechas por saturación (HU-48), sobre un almacén en memoria que simula
 * cortes de energía, escrituras parciales y registros corruptos.
 */
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "core/ColaLecturas.h"

using core::EstadoCabeza;
using core::ResultadoEnvio;
using core::ResultadoGuardado;

// ── Dobles de prueba ───────────────────────────────────────────────────────

class AlmacenMemoria : public core::AlmacenSegmentos {
public:
    std::map<uint32_t, std::vector<uint8_t>> segmentos;
    std::map<std::string, std::vector<uint8_t>> meta;
    std::vector<uint8_t> cuarentena;
    size_t libres = 1024 * 1024;
    int cortarProximaEscritura = -1;  ///< ≥0: la próxima escritura solo graba N bytes

    std::vector<uint32_t> listarSegmentos() override {
        std::vector<uint32_t> ids;
        for (const auto& kv : segmentos) ids.push_back(kv.first);
        return ids;
    }
    size_t tamanoSegmento(uint32_t id) override {
        auto it = segmentos.find(id);
        return it == segmentos.end() ? 0 : it->second.size();
    }
    size_t anexar(uint32_t id, const uint8_t* d, size_t n) override {
        size_t aEscribir = n;
        if (cortarProximaEscritura >= 0) {
            aEscribir = (size_t)cortarProximaEscritura < n ? (size_t)cortarProximaEscritura : n;
            cortarProximaEscritura = -1;
        }
        auto& v = segmentos[id];
        v.insert(v.end(), d, d + aEscribir);
        return aEscribir;
    }
    bool leer(uint32_t id, size_t off, uint8_t* d, size_t n) override {
        auto it = segmentos.find(id);
        if (it == segmentos.end() || off + n > it->second.size()) return false;
        for (size_t i = 0; i < n; ++i) d[i] = it->second[off + i];
        return true;
    }
    bool borrarSegmento(uint32_t id) override { return segmentos.erase(id) > 0; }
    bool leerMeta(const char* nombre, uint8_t* d, size_t n) override {
        auto it = meta.find(nombre);
        if (it == meta.end() || it->second.size() != n) return false;
        for (size_t i = 0; i < n; ++i) d[i] = it->second[i];
        return true;
    }
    bool escribirMeta(const char* nombre, const uint8_t* d, size_t n) override {
        meta[nombre] = std::vector<uint8_t>(d, d + n);
        return true;
    }
    size_t tamanoCuarentena() override { return cuarentena.size(); }
    size_t anexarCuarentena(const uint8_t* d, size_t n) override {
        cuarentena.insert(cuarentena.end(), d, d + n);
        return n;
    }
    size_t bytesLibres() override { return libres; }
};

class PerdidasMemoria : public core::PersistenciaPerdidas {
public:
    core::EstadoPerdidas guardado;
    bool hay = false;
    int escrituras = 0;
    bool cargar(core::EstadoPerdidas& e) override {
        if (!hay) return false;
        e = guardado;
        return true;
    }
    bool guardar(const core::EstadoPerdidas& e) override {
        guardado = e;
        hay = true;
        escrituras++;
        return true;
    }
};

/// Devuelve los resultados del guion en orden; agotado, `porDefecto`.
class TransmisorGuion : public core::Transmisor {
public:
    std::vector<ResultadoEnvio> guion;
    ResultadoEnvio porDefecto = ResultadoEnvio::Confirmado;
    std::vector<uint32_t> enviados;
    size_t i = 0;
    ResultadoEnvio enviar(const core::RegistroLectura& r) override {
        enviados.push_back(r.seqNo);
        return i < guion.size() ? guion[i++] : porDefecto;
    }
};

static core::RegistroLectura lectura(uint32_t seq, uint32_t boot = 1) {
    core::RegistroLectura r;
    r.bootId = boot;
    r.seqNo = seq;
    r.uptimeS = seq * 30;
    r.capturadoEpoch = 1790350200u + seq * 30;
    r.poner(core::BANDERA_TIEMPO_SNTP, true);
    core::fijarDS18B20(r, core::LecturaSensor::ok(4.0f + (float)(seq % 4)));
    core::LecturaSHT31 sht;
    sht.temperatura = core::LecturaSensor::ok(22.0f);
    sht.humedad = core::LecturaSensor::ok(55.0f);
    core::fijarSHT31(r, sht);
    return r;
}

static core::ConfigCola config(uint32_t porSegmento, uint32_t maxSegmentos) {
    core::ConfigCola c;
    c.registrosPorSegmento = porSegmento;
    c.maxSegmentos = maxSegmentos;
    c.reservaBytesLibres = 0;
    return c;
}

static void guardarN(core::ColaLecturas& cola, uint32_t desde, uint32_t hasta) {
    for (uint32_t s = desde; s <= hasta; ++s) {
        TEST_ASSERT_TRUE(cola.guardar(lectura(s)) == ResultadoGuardado::Guardado);
    }
}

// ── Pruebas ────────────────────────────────────────────────────────────────

void test_fifo_estricto_entre_segmentos(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    guardarN(cola, 1, 7);
    TEST_ASSERT_EQUAL_UINT32(3, cola.segmentos());
    TEST_ASSERT_EQUAL_UINT32(7, cola.pendientes());

    TransmisorGuion tx;
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100);
    TEST_ASSERT_EQUAL_INT(7, r.confirmados);
    TEST_ASSERT_EQUAL_UINT32(7, (uint32_t)tx.enviados.size());
    for (uint32_t k = 0; k < 7; ++k) TEST_ASSERT_EQUAL_UINT32(k + 1, tx.enviados[k]);
    TEST_ASSERT_EQUAL_UINT32(0, cola.pendientes());
    // Los segmentos llenos y con acuse completo se borraron; el abierto sigue.
    TEST_ASSERT_EQUAL_UINT32(1, cola.segmentos());
}

/// REGLA DE ORO: sin acuse lógico (p. ej. hubo PUBACK pero el backend no
/// confirmó el COMMIT) no se borra nada y el drenaje se detiene.
void test_sin_acuse_logico_no_se_borra_nada(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    guardarN(cola, 1, 7);

    TransmisorGuion tx;
    tx.porDefecto = ResultadoEnvio::Fallo;
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100);
    TEST_ASSERT_TRUE(r.detenido);
    TEST_ASSERT_EQUAL_INT(0, r.confirmados);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)tx.enviados.size());  // se detuvo en el primero
    TEST_ASSERT_EQUAL_UINT32(7, cola.pendientes());
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)a.segmentos.size());
}

/// HU-07 esc. 2: tras una caída a mitad de la sincronización se reanuda desde
/// el primer registro SIN acuse, no desde el principio ni saltándose ninguno.
void test_reanuda_desde_el_ultimo_confirmado(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    guardarN(cola, 1, 6);

    TransmisorGuion tx1;
    tx1.guion = {ResultadoEnvio::Confirmado, ResultadoEnvio::Confirmado, ResultadoEnvio::SinConexion};
    core::drenar(cola, tx1, 100);
    TEST_ASSERT_EQUAL_UINT32(4, cola.pendientes());

    TransmisorGuion tx2;
    core::drenar(cola, tx2, 100);
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)tx2.enviados.size());
    TEST_ASSERT_EQUAL_UINT32(3, tx2.enviados[0]);  // el no acusado se reenvía
    TEST_ASSERT_EQUAL_UINT32(6, tx2.enviados[3]);
}

void test_aviso_80_por_ciento_una_vez_y_se_rearma(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(10, 10));  // capacidad 100
    cola.montar();
    uint8_t pct = 0;

    guardarN(cola, 1, 79);
    TEST_ASSERT_FALSE(cola.consumirAvisoOcupacion(pct));
    guardarN(cola, 80, 80);
    TEST_ASSERT_TRUE(cola.consumirAvisoOcupacion(pct));
    TEST_ASSERT_EQUAL_UINT8(80, pct);
    guardarN(cola, 81, 85);
    TEST_ASSERT_FALSE(cola.consumirAvisoOcupacion(pct));  // una sola vez

    TransmisorGuion tx;
    core::drenar(cola, tx, 30);  // libera 3 segmentos → 55 %
    TEST_ASSERT_TRUE(cola.ocupacionPct() < 70);
    guardarN(cola, 86, 110);
    TEST_ASSERT_TRUE(cola.consumirAvisoOcupacion(pct));  // se rearmó
}

/// HU-06 esc. 3 / HU-48 esc. 2: al 100 % se preservan las lecturas antiguas,
/// la nueva no entra y la pérdida queda contada y persistida con su periodo.
void test_saturacion_preserva_antiguas_y_cuenta_perdidas(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(2, 3));  // capacidad 6
    cola.montar();
    guardarN(cola, 1, 6);
    TEST_ASSERT_EQUAL_UINT8(100, cola.ocupacionPct());

    for (uint32_t s = 7; s <= 9; ++s) {
        TEST_ASSERT_TRUE(cola.guardar(lectura(s)) == ResultadoGuardado::SaturadoNoAlmacenado);
    }
    TEST_ASSERT_EQUAL_UINT32(6, cola.pendientes());
    TEST_ASSERT_EQUAL_UINT32(3, p.guardado.total);  // persistido fuera de LittleFS
    TEST_ASSERT_TRUE(p.guardado.episodioAbierto);
    TEST_ASSERT_EQUAL_UINT32(7, p.guardado.desdeSeq);
    TEST_ASSERT_EQUAL_UINT32(9, p.guardado.hastaSeq);

    // La más antigua sigue intacta: nada se sobrescribió.
    core::RegistroLectura cabeza;
    uint8_t bruto[core::TAM_REGISTRO];
    TEST_ASSERT_TRUE(cola.leerCabeza(cabeza, bruto) == EstadoCabeza::Ok);
    TEST_ASSERT_EQUAL_UINT32(1, cabeza.seqNo);

    core::EstadoPerdidas e;
    TEST_ASSERT_TRUE(cola.consumirAvisoPerdidas(e));
    TEST_ASSERT_EQUAL_UINT32(3, e.episodio);

    // Al volver la conexión se libera espacio y el episodio se cierra.
    TransmisorGuion tx;
    core::drenar(cola, tx, 100);
    TEST_ASSERT_TRUE(cola.guardar(lectura(10)) == ResultadoGuardado::Guardado);
    TEST_ASSERT_TRUE(cola.consumirAvisoPerdidas(e));
    TEST_ASSERT_FALSE(e.episodioAbierto);
    TEST_ASSERT_EQUAL_UINT32(3, e.total);
    TEST_ASSERT_EQUAL_UINT32(9, e.hastaSeq);
}

void test_reinicio_recupera_pendientes_y_cursor(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    {
        core::ColaLecturas cola(a, p, config(2, 10));
        cola.montar();
        guardarN(cola, 1, 5);
        TransmisorGuion tx;
        tx.guion = {ResultadoEnvio::Confirmado, ResultadoEnvio::Confirmado,
                    ResultadoEnvio::Confirmado, ResultadoEnvio::Fallo};
        core::drenar(cola, tx, 100);
        TEST_ASSERT_EQUAL_UINT32(2, cola.pendientes());
    }
    // "Corte de energía": nueva instancia sobre el mismo almacén.
    core::ColaLecturas cola(a, p, config(2, 10));
    const core::InformeMontaje inf = cola.montar();
    TEST_ASSERT_FALSE(inf.cursorReconstruido);
    TEST_ASSERT_EQUAL_UINT32(2, inf.pendientes);
    core::RegistroLectura r;
    uint8_t bruto[core::TAM_REGISTRO];
    TEST_ASSERT_TRUE(cola.leerCabeza(r, bruto) == EstadoCabeza::Ok);
    TEST_ASSERT_EQUAL_UINT32(4, r.seqNo);
}

/// HU-06 esc. 2: un corte a mitad de escritura deja una cola parcial. Se
/// recuperan los registros completos, se ignora solo lo incompleto y ningún
/// registro nuevo queda desalineado detrás.
void test_corte_a_mitad_de_escritura(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    {
        core::ColaLecturas cola(a, p, config(10, 10));
        cola.montar();
        guardarN(cola, 1, 3);
        a.cortarProximaEscritura = 17;
        TEST_ASSERT_TRUE(cola.guardar(lectura(4)) == ResultadoGuardado::ErrorEscritura);
        TEST_ASSERT_EQUAL_UINT32(1, cola.perdidas().total);  // tampoco es silenciosa
    }
    core::ColaLecturas cola(a, p, config(10, 10));
    const core::InformeMontaje inf = cola.montar();
    TEST_ASSERT_EQUAL_UINT32(1, inf.segmentosConColaParcial);
    TEST_ASSERT_EQUAL_UINT32(17, inf.bytesParcialesIgnorados);
    TEST_ASSERT_EQUAL_UINT32(3, inf.pendientes);

    guardarN(cola, 5, 5);  // va a un segmento nuevo
    TEST_ASSERT_EQUAL_UINT32(2, cola.segmentos());
    TransmisorGuion tx;
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100);
    TEST_ASSERT_EQUAL_INT(4, r.confirmados);
    TEST_ASSERT_EQUAL_INT(0, r.corruptos);
    TEST_ASSERT_EQUAL_UINT32(5, tx.enviados[3]);
}

void test_registro_corrupto_va_a_cuarentena_sin_bloquear(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(10, 10));
    cola.montar();
    guardarN(cola, 1, 3);
    a.segmentos.begin()->second[core::TAM_REGISTRO + 10] ^= 0xFF;  // daña el 2.º

    TransmisorGuion tx;
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100);
    TEST_ASSERT_EQUAL_INT(1, r.corruptos);
    TEST_ASSERT_EQUAL_INT(2, r.confirmados);
    TEST_ASSERT_EQUAL_UINT32(1, tx.enviados[0]);
    TEST_ASSERT_EQUAL_UINT32(3, tx.enviados[1]);  // el corrupto nunca se publicó
    TEST_ASSERT_EQUAL_UINT32(44, (uint32_t)a.cuarentena.size());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)core::MotivoCuarentena::CrcInvalido, a.cuarentena[0]);
    TEST_ASSERT_EQUAL_UINT32(1, cola.cuarentenados());
}

/// Un rechazo permanente del backend no se borra: se conserva en cuarentena
/// y el FIFO sigue con las lecturas válidas.
void test_rechazado_por_backend_va_a_cuarentena(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(10, 10));
    cola.montar();
    guardarN(cola, 1, 3);

    TransmisorGuion tx;
    tx.guion = {ResultadoEnvio::Confirmado, ResultadoEnvio::Rechazado, ResultadoEnvio::Invalido};
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100);
    TEST_ASSERT_EQUAL_INT(1, r.rechazados);
    TEST_ASSERT_EQUAL_INT(1, r.invalidos);
    TEST_ASSERT_EQUAL_UINT32(0, cola.pendientes());
    TEST_ASSERT_EQUAL_UINT32(88, (uint32_t)a.cuarentena.size());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)core::MotivoCuarentena::RechazadoBackend, a.cuarentena[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)core::MotivoCuarentena::PayloadInvalido, a.cuarentena[44]);

    core::RegistroLectura copia;
    TEST_ASSERT_TRUE(core::decodificarRegistro(&a.cuarentena[4], copia));
    TEST_ASSERT_EQUAL_UINT32(2, copia.seqNo);  // copia íntegra del registro rechazado
}

/// Sin cursor fiable se reenvía todo: duplicar es inofensivo (idempotencia
/// HU-11 en el backend), perder no.
void test_cursor_perdido_reenvia_desde_el_inicio(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    {
        core::ColaLecturas cola(a, p, config(10, 10));
        cola.montar();
        guardarN(cola, 1, 4);
        TransmisorGuion tx;
        tx.guion = {ResultadoEnvio::Confirmado, ResultadoEnvio::Confirmado, ResultadoEnvio::Fallo};
        core::drenar(cola, tx, 100);
    }
    a.meta.clear();
    core::ColaLecturas cola(a, p, config(10, 10));
    const core::InformeMontaje inf = cola.montar();
    TEST_ASSERT_TRUE(inf.cursorReconstruido);
    TEST_ASSERT_EQUAL_UINT32(4, inf.pendientes);
}

/// Corte entre borrar un segmento consumido y actualizar el cursor.
void test_corte_entre_borrado_y_cursor(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    {
        core::ColaLecturas cola(a, p, config(2, 10));
        cola.montar();
        guardarN(cola, 1, 4);
        TransmisorGuion tx;
        tx.guion = {ResultadoEnvio::Confirmado, ResultadoEnvio::Fallo};
        core::drenar(cola, tx, 100);  // cursor = (seg 1, idx 1)
    }
    a.segmentos.erase(a.segmentos.begin());  // el segmento 1 alcanzó a borrarse
    core::ColaLecturas cola(a, p, config(2, 10));
    const core::InformeMontaje inf = cola.montar();
    TEST_ASSERT_EQUAL_UINT32(2, inf.pendientes);
    core::RegistroLectura r;
    uint8_t bruto[core::TAM_REGISTRO];
    TEST_ASSERT_TRUE(cola.leerCabeza(r, bruto) == EstadoCabeza::Ok);
    TEST_ASSERT_EQUAL_UINT32(3, r.seqNo);
}

void test_sin_margen_en_el_sistema_de_archivos_es_saturacion(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ConfigCola c = config(10, 10);
    c.reservaBytesLibres = 16 * 1024;
    core::ColaLecturas cola(a, p, c);
    cola.montar();
    a.libres = 1000;
    TEST_ASSERT_TRUE(cola.guardar(lectura(1)) == ResultadoGuardado::SaturadoNoAlmacenado);
    TEST_ASSERT_EQUAL_UINT32(1, p.guardado.total);
}

/// Un episodio de saturación abierto al reiniciar se vuelve a reportar.
void test_perdidas_sobreviven_al_reinicio(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    {
        core::ColaLecturas cola(a, p, config(1, 1));
        cola.montar();
        guardarN(cola, 1, 1);
        cola.guardar(lectura(2));
        cola.guardar(lectura(3));
    }
    core::ColaLecturas cola(a, p, config(1, 1));
    cola.montar();
    core::EstadoPerdidas e;
    TEST_ASSERT_TRUE(cola.consumirAvisoPerdidas(e));
    TEST_ASSERT_EQUAL_UINT32(2, e.total);
    TEST_ASSERT_TRUE(e.episodioAbierto);
}

class CerrojoEspia : public core::Cerrojo {
public:
    int profundidad = 0;
    int tomas = 0;
    void tomar() override { profundidad++; tomas++; }
    void soltar() override { profundidad--; }
};

class TransmisorQueVigilaElCerrojo : public core::Transmisor {
public:
    CerrojoEspia* cerrojo = nullptr;
    bool sostenidoDuranteEnvio = false;
    ResultadoEnvio enviar(const core::RegistroLectura&) override {
        if (cerrojo->profundidad != 0) sostenidoDuranteEnvio = true;
        return ResultadoEnvio::Confirmado;
    }
};

/// El mutex del buffer jamás se mantiene mientras se espera el PUBACK y el
/// acuse lógico: la tarea de sensores nunca debe esperar a la red.
void test_el_cerrojo_no_se_sostiene_durante_el_envio(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    guardarN(cola, 1, 5);
    CerrojoEspia cerrojo;
    TransmisorQueVigilaElCerrojo tx;
    tx.cerrojo = &cerrojo;
    const core::ResumenDrenaje r = core::drenar(cola, tx, 100, &cerrojo);
    TEST_ASSERT_EQUAL_INT(5, r.confirmados);
    TEST_ASSERT_FALSE(tx.sostenidoDuranteEnvio);
    TEST_ASSERT_EQUAL_INT(0, cerrojo.profundidad);
    TEST_ASSERT_TRUE(cerrojo.tomas >= 10);
}

/// Tablas de particiones: LittleFS debe alojar las 30 000 lecturas nominales.
void test_particiones_alojan_la_capacidad_nominal(void) {
    const core::ConfigCola config;  // 300 segmentos × 100 registros, reserva 16 KB
    TEST_ASSERT_EQUAL_UINT32(1310720u, (uint32_t)core::bytesMinimosParticion(config));  // 1280 KB
    TEST_ASSERT_TRUE(0x170000u >= core::bytesMinimosParticion(config));  // partitions.csv
    TEST_ASSERT_TRUE(0x160000u >= core::bytesMinimosParticion(config));  // partitions_produccion.csv
}

/// ADR-18, `buffer descartar`: se borra por tandas lo que había al pedirlo; lo
/// que llega después, la cuarentena y el cursor tras reiniciar quedan bien.
void test_descartar_por_tandas_respeta_lo_guardado_despues(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    guardarN(cola, 1, 7);  // segmentos {1,2,3} {4,5,6} {7}
    TransmisorGuion tx;
    core::drenar(cola, tx, 2);  // 1 y 2 con acuse: quedan 5 pendientes
    TEST_ASSERT_EQUAL_UINT32(5, cola.pendientes());

    const uint32_t ultimo = cola.prepararDescarte();
    guardarN(cola, 8, 8);  // llega después: segmento nuevo, fuera del descarte

    bool quedan = false;
    uint32_t descartados = cola.descartarTanda(ultimo, 2, quedan);
    TEST_ASSERT_EQUAL_UINT32(4, descartados);  // 3 del primer segmento y 4, 5, 6
    TEST_ASSERT_TRUE(quedan);
    descartados += cola.descartarTanda(ultimo, 2, quedan);
    TEST_ASSERT_EQUAL_UINT32(5, descartados);
    TEST_ASSERT_FALSE(quedan);
    TEST_ASSERT_EQUAL_UINT32(1, cola.pendientes());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)a.cuarentena.size());

    core::ColaLecturas tras(a, p, config(3, 10));
    tras.montar();
    TEST_ASSERT_EQUAL_UINT32(1, tras.pendientes());
    core::RegistroLectura r;
    uint8_t bruto[core::TAM_REGISTRO];
    TEST_ASSERT_TRUE(tras.leerCabeza(r, bruto) == EstadoCabeza::Ok);
    TEST_ASSERT_EQUAL_UINT32(8, r.seqNo);
}

void test_descartar_con_la_cola_vacia_no_hace_nada(void) {
    AlmacenMemoria a;
    PerdidasMemoria p;
    core::ColaLecturas cola(a, p, config(3, 10));
    cola.montar();
    const uint32_t ultimo = cola.prepararDescarte();
    TEST_ASSERT_EQUAL_UINT32(0, ultimo);
    bool quedan = true;
    TEST_ASSERT_EQUAL_UINT32(0, cola.descartarTanda(ultimo, 10, quedan));
    TEST_ASSERT_FALSE(quedan);
}

void run_tests_cola(void) {
    RUN_TEST(test_particiones_alojan_la_capacidad_nominal);
    RUN_TEST(test_el_cerrojo_no_se_sostiene_durante_el_envio);
    RUN_TEST(test_fifo_estricto_entre_segmentos);
    RUN_TEST(test_sin_acuse_logico_no_se_borra_nada);
    RUN_TEST(test_reanuda_desde_el_ultimo_confirmado);
    RUN_TEST(test_aviso_80_por_ciento_una_vez_y_se_rearma);
    RUN_TEST(test_saturacion_preserva_antiguas_y_cuenta_perdidas);
    RUN_TEST(test_reinicio_recupera_pendientes_y_cursor);
    RUN_TEST(test_corte_a_mitad_de_escritura);
    RUN_TEST(test_registro_corrupto_va_a_cuarentena_sin_bloquear);
    RUN_TEST(test_rechazado_por_backend_va_a_cuarentena);
    RUN_TEST(test_cursor_perdido_reenvia_desde_el_inicio);
    RUN_TEST(test_corte_entre_borrado_y_cursor);
    RUN_TEST(test_sin_margen_en_el_sistema_de_archivos_es_saturacion);
    RUN_TEST(test_perdidas_sobreviven_al_reinicio);
    RUN_TEST(test_descartar_por_tandas_respeta_lo_guardado_despues);
    RUN_TEST(test_descartar_con_la_cola_vacia_no_hace_nada);
}
