/**
 * ThermoTrace Edge — firmware del nodo IoT (ESP32 DevKitC V4)
 * ============================================================
 * Tesis: Sistema IoT con IA y trazabilidad digital verificable para el
 * monitoreo de la cadena de frío de medicamentos termolábiles (Lima).
 * Capa Edge — responsable: Brenda Lucía Gamio Upiachihua.
 *
 * Dos tareas FreeRTOS en el núcleo 1 (el 0 queda para la pila Wi-Fi):
 *
 *   Captura (prioridad 3) — cada intervalo: DS18B20 + SHT31 + MC-38 →
 *       registro binario → LittleFS. SIEMPRE se guarda primero (offline-first):
 *       la captura nunca espera a la red.
 *   Red     (prioridad 2) — Wi-Fi con backoff exponencial, SNTP, canal
 *       MQTT/TLS hacia EMQX Cloud (solo 8883, CA validada), eventos de
 *       conectividad y diagnóstico (online, LWT, arranque, buffer), drenaje
 *       FIFO del buffer con acuse lógico y consola serie.
 *
 * Flujo de una lectura:
 *   sensores → RegistroLectura (CRC32) → LittleFS
 *            → JSON v1.0 → PUBLISH QoS 1 → PUBACK (broker)
 *            → acuse lógico del backend tras COMMIT en PostgreSQL
 *            → recién ahí se libera de LittleFS.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include "config.h"
#include "core/Backoff.h"
#include "core/Consola.h"
#include "core/Json.h"
#include "core/Payload.h"
#include "core/Registro.h"
#include "core/Tiempo.h"
#include "network/MQTTManager.h"
#include "network/TransmisorMQTT.h"
#include "network/WiFiManager.h"
#include "sensors/DS18B20Sensor.h"
#include "sensors/MC38Sensor.h"
#include "sensors/SHT31Sensor.h"
#include "storage/BufferLittleFS.h"
#include "system/Configuracion.h"
#include "system/Eventos.h"
#include "system/PowerBackup.h"
#include "system/Reloj.h"
#include "system/Watchdog.h"

// =========================================================================
// Estado global
// =========================================================================
static ConfiguracionNodo g_config;
static uint32_t g_bootId = 0;
static uint32_t g_seqNo = 0;  // solo lo escribe la tarea de captura

static DS18B20Sensor ds18b20(PIN_DS18B20);
static SHT31Sensor sht31(SHT31_DIRECCION, PIN_I2C_SDA, PIN_I2C_SCL);
#if MC38_INSTALADO
static MC38Sensor mc38(PIN_MC38);
#endif
static BufferLittleFS buffer;
static WiFiClientSecure tls;
static WiFiManager wifi;
static MQTTManager mqtt(tls);

static volatile bool g_revisarTokenYa = false;

// IOT-002: consola de escritura bloqueada en un nodo aprovisionado con clave
// de servicio. La consola se atiende solo en la tarea de red.
static core::EstadoConsola g_consola;
static core::FrenoIntentos g_frenoConsola;
static String g_huellaServicio;
static bool g_aprovisionadoAlArrancar = false;

static void publicarEventoDirecto(const char* tipo, const std::string& detalle);

#if MODO_DEMO
// Excursión SIMULADA para la demo/sustentación. Cada lectura afectada lleva
// "simulated": true. Este código no existe en el firmware de producción.
static volatile uint32_t g_demoRestantes = 0;
static volatile int16_t g_demoTempCenti = 1250;  // 12,50 °C
#endif

// =========================================================================
// Utilidades
// =========================================================================
static const char* nombreReinicio(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

static const char* textoGuardado(core::ResultadoGuardado g) {
    switch (g) {
        case core::ResultadoGuardado::Guardado:             return "OK";
        case core::ResultadoGuardado::SaturadoNoAlmacenado: return "SATURADO";
        default:                                            return "ERROR_ESCRITURA";
    }
}

static void textoCentesimas(char* buf, size_t n, int32_t c, bool nulo) {
    if (nulo) {
        snprintf(buf, n, "NULL");
        return;
    }
    const int32_t a = c < 0 ? -c : c;
    snprintf(buf, n, "%s%ld.%02ld", c < 0 ? "-" : "", (long)(a / 100), (long)(a % 100));
}

// =========================================================================
// Captura
// =========================================================================
static core::RegistroLectura capturar() {
    core::RegistroLectura r;
    r.bootId = g_bootId;
    r.seqNo = ++g_seqNo;
    r.uptimeS = uptimeSegundos();
    const uint32_t epoch = epochAhora();  // 0 si no hay SNTP: no se inventa la hora
    if (epoch != 0) {
        r.capturadoEpoch = epoch;
        r.poner(core::BANDERA_TIEMPO_SNTP, true);
    }

    // Nunca un código de error como valor: una falla sale como
    // SENSOR_FAULT con su motivo (HU-01 esc. 2 y 3).
    core::fijarDS18B20(r, ds18b20.leer());
    alimentarWatchdog();
    core::fijarSHT31(r, sht31.leer());  // independiente del DS18B20

#if MC38_INSTALADO
    r.poner(core::BANDERA_MC38_INSTALADO, true);
    r.poner(core::BANDERA_PUERTA_ABIERTA, mc38.huboApertura());
    const uint32_t abiertaS = mc38.segundosAbiertaEnVentana();
    const uint32_t rachaS = mc38.rachaSegundos();
    r.puertaAbiertaS = (uint16_t)(abiertaS > 65535u ? 65535u : abiertaS);
    r.puertaRachaS = rachaS >= UMBRAL_PUERTA_PROLONGADA_S ? (uint16_t)(rachaS > 65535u ? 65535u : rachaS) : 0;
    mc38.cerrarVentana();
#endif

    // HU-52: con sensado instalado cada lectura registra si se capturó con red
    // comercial o con el Mini UPS (evidencia persistida aunque no haya red).
    if (energiaSensada()) {
        r.poner(core::BANDERA_SENSADO_ENERGIA, true);
        r.poner(core::BANDERA_EN_RESPALDO, enRespaldo());
    }
    r.fwMayor = FW_MAYOR;
    r.fwMenor = FW_MENOR;
    r.fwParche = FW_PARCHE;

#if MODO_DEMO
    if (g_demoRestantes > 0) {
        g_demoRestantes = g_demoRestantes - 1;
        r.tempDsCenti = g_demoTempCenti;
        r.falloDs = core::FalloSensor::Ninguno;
        r.poner(core::BANDERA_SIMULADO, true);
    }
#endif
    return r;
}

/// Línea fija por captura. La consumen los scripts de QA (ensayo doméstico
/// de 2–3 h): no cambiar el formato sin actualizar analizar_ensayo.py.
static void imprimirCaptura(const core::RegistroLectura& r, core::ResultadoGuardado g) {
    char ds[16], sht[16], hr[16];
    textoCentesimas(ds, sizeof(ds), r.tempDsCenti, r.falloDs != core::FalloSensor::Ninguno);
    textoCentesimas(sht, sizeof(sht), r.tempShtCenti, r.falloShtT != core::FalloSensor::Ninguno);
    textoCentesimas(hr, sizeof(hr), r.humShtCenti, r.falloShtH != core::FalloSensor::Ninguno);
    const char* dsF = core::codigoFallo(r.falloDs);
    const char* shF = core::codigoFallo(r.falloShtT != core::FalloSensor::Ninguno ? r.falloShtT : r.falloShtH);
    const char* puerta = r.tiene(core::BANDERA_MC38_INSTALADO)
                             ? (r.tiene(core::BANDERA_PUERTA_ABIERTA) ? "1" : "0")
                             : "NA";
    const std::string t = r.tiene(core::BANDERA_TIEMPO_SNTP) ? core::formatearISO8601(r.capturadoEpoch)
                                                             : std::string("-");
    Serial.printf("[CAP] boot=%lu seq=%lu up=%lu t=%s tq=%s ds=%s ds_f=%s sht=%s hr=%s sht_f=%s "
                  "door=%s door_s=%u pwr=%s sim=%d g=%s q=%lu ocup=%u heap=%lu\n",
                  (unsigned long)r.bootId, (unsigned long)r.seqNo, (unsigned long)r.uptimeS, t.c_str(),
                  r.tiene(core::BANDERA_TIEMPO_SNTP) ? "sntp" : "nosync", ds, dsF ? dsF : "-", sht,
                  hr, shF ? shF : "-", puerta, (unsigned)r.puertaAbiertaS,
                  r.tiene(core::BANDERA_SENSADO_ENERGIA)
                      ? (r.tiene(core::BANDERA_EN_RESPALDO) ? "backup" : "mains")
                      : "NA",
                  r.tiene(core::BANDERA_SIMULADO) ? 1 : 0, textoGuardado(g),
                  (unsigned long)buffer.pendientes(), (unsigned)buffer.ocupacionPct(),
                  (unsigned long)ESP.getFreeHeap());
}

#if MODO_DEMO
static void revisarBotonDemo() {
    static uint32_t presionadoDesde = 0;
    static bool disparado = false;
    const bool presionado = digitalRead(PIN_BOTON_DEMO) == LOW;
    if (!presionado) {
        presionadoDesde = 0;
        disparado = false;
        return;
    }
    if (presionadoDesde == 0) presionadoDesde = millis();
    if (!disparado && millis() - presionadoDesde > 1000) {  // pulsación larga: evita toques accidentales
        disparado = true;
        g_demoTempCenti = 1250;
        g_demoRestantes = 4;
        LOG_I("DEMO", "Excursion SIMULADA: 12.50 C en las proximas 4 lecturas (simulated=true).");
    }
}
#endif

static void taskCaptura(void*) {
    suscribirTareaAlWatchdog("Captura");
    TickType_t ultimo = xTaskGetTickCount();
    const TickType_t paso = pdMS_TO_TICKS(MC38_POLL_MS);

    for (;;) {
        const core::RegistroLectura r = capturar();
        const core::ResultadoGuardado g = buffer.guardar(r);
        if (g != core::ResultadoGuardado::Guardado) {
            LOG_E("Captura", "Lectura seq=%lu NO almacenada (%s): contada como perdida.",
                  (unsigned long)r.seqNo, textoGuardado(g));
        }
        imprimirCaptura(r, g);

        // Espera hasta el próximo ciclo muestreando puerta y energía cada 50 ms.
        const TickType_t intervalo = pdMS_TO_TICKS(g_config.intervaloMuestreoS * 1000UL);
        for (;;) {
            const int32_t faltan = (int32_t)(ultimo + intervalo - xTaskGetTickCount());
            if (faltan <= (int32_t)paso) break;
            vTaskDelay(paso);
#if MC38_INSTALADO
            // HU-04 esc. 1: cada cambio estable de la puerta queda registrado
            // con su propio timestamp (evento "puerta"), además del resumen
            // por ventana que viaja en la telemetría.
            if (mc38.poll()) {
                core::ObjetoJSON d;
                d.booleano("door_open", mc38.abierta());
                encolarEvento("puerta", d.cerrar());
            }
#endif
            muestrearEnergia();  // conmutación a respaldo con timestamp (HU-52 esc. 1)
#if MODO_DEMO
            revisarBotonDemo();
#endif
            alimentarWatchdog();
        }
        vTaskDelayUntil(&ultimo, intervalo);
    }
}

// =========================================================================
// Consola serie (aprovisionamiento sin recompilar y sin secretos en el repo)
// =========================================================================
static void imprimirEstado() {
    const core::EstadoPerdidas p = buffer.perdidas();
    Serial.printf("\n== ThermoTrace Edge v%d.%d.%d ==\n", FW_MAYOR, FW_MENOR, FW_PARCHE);
    Serial.printf("device_id=%s boot_id=%lu seq_no=%lu uptime=%lus intervalo=%lus\n",
                  g_config.deviceId.c_str(), (unsigned long)g_bootId, (unsigned long)g_seqNo,
                  (unsigned long)uptimeSegundos(), (unsigned long)g_config.intervaloMuestreoS);
    Serial.printf("wifi=%s rssi=%d dBm intentos_fallidos=%lu retardo=%lums diag=%d | mqtt=%s ca=%d aprov=%d\n",
                  wifi.conectado() ? "OK" : "NO", wifi.rssi(), (unsigned long)wifi.intentosFallidos(),
                  (unsigned long)wifi.retardoActualMs(), wifi.diagnosticoActivo() ? 1 : 0,
                  mqtt.conectado() ? "OK" : "NO", mqtt.caCargada() ? 1 : 0, mqtt.aprovisionado() ? 1 : 0);
    Serial.printf("reloj=%s | buffer: pendientes=%lu/%lu ocupacion=%u%% libres=%luKB cuarentena=%lu\n",
                  relojSincronizado() ? "sntp_synced" : "unsynced", (unsigned long)buffer.pendientes(),
                  (unsigned long)buffer.capacidad(), (unsigned)buffer.ocupacionPct(),
                  (unsigned long)(buffer.bytesLibres() / 1024), (unsigned long)buffer.cuarentenados());
    Serial.printf("perdidas: total=%lu episodio=%lu abierto=%d | energia=%s | heap=%lu (min %lu)\n",
                  (unsigned long)p.total, (unsigned long)p.episodio, p.episodioAbierto ? 1 : 0,
                  energiaSensada() ? (enRespaldo() ? "RESPALDO" : "red") : "sin_sensado",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
}

static void imprimirRssi() {
    if (!wifi.conectado()) {
        Serial.println("[C-5] Sin Wi-Fi: no hay RSSI que medir.");
        return;
    }
    long suma = 0;
    int minimo = 0, maximo = -200;
    const int n = 10;
    for (int i = 0; i < n; ++i) {
        const int v = WiFi.RSSI();
        suma += v;
        if (v < minimo) minimo = v;
        if (v > maximo) maximo = v;
        alimentarWatchdog();
        delay(200);
    }
    const long promedio = suma / n;
    Serial.printf("[C-5] SSID=%s canal=%d RSSI promedio=%ld dBm (min %d, max %d, n=%d) -> %s\n",
                  WiFi.SSID().c_str(), (int)WiFi.channel(), promedio, minimo, maximo, n,
                  promedio >= -65 ? "CONFORME (>= -65 dBm)" : "NO CONFORME (< -65 dBm)");
}

static void atenderConsola() {
    static String linea;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') {
            if (linea.length() < 200) linea += c;
            continue;
        }
        String cmd = linea;
        linea = "";
        cmd.trim();
        if (cmd.isEmpty()) continue;

        const int e1 = cmd.indexOf(' ');
        const String verbo = e1 < 0 ? cmd : cmd.substring(0, e1);
        String resto = e1 < 0 ? String() : cmd.substring(e1 + 1);
        resto.trim();

        // IOT-002: con la consola bloqueada solo se aceptan comandos de lectura.
        const bool hayClave = !g_huellaServicio.isEmpty();
        if (core::comandoModifica(std::string(verbo.c_str())) &&
            g_consola.bloqueada(g_aprovisionadoAlArrancar, hayClave, millis())) {
            Serial.println("Consola bloqueada: use 'desbloquear <clave_servicio>'.");
            continue;
        }

        if (verbo == "ayuda" || verbo == "help") {
            Serial.println("Comandos: estado | rssi | nvs set <clave> <valor> | nvs borrar <clave> | "
                           "reiniciar | desbloquear <clave_servicio> | bloquear"
#if MODO_DEMO
                           " | demo excursion <temp_C> <n_lecturas>"
#endif
            );
            Serial.println("Claves NVS: wifi_ssid wifi_pass mqtt_host mqtt_token device_id intervalo_s "
                           "clave_servicio");
        } else if (verbo == "desbloquear") {
            if (!hayClave) {
                Serial.println("Sin clave de servicio: la consola no esta bloqueada.");
            } else if (!g_frenoConsola.puedeIntentar(millis())) {
                Serial.println("Demasiados intentos fallidos: espere 10 minutos.");
            } else if (core::claveServicioCorrecta(std::string(resto.c_str()),
                                                   std::string(g_huellaServicio.c_str()))) {
                g_frenoConsola.registrarAcierto();
                g_consola.desbloquear(millis());
                Serial.println("Consola desbloqueada durante 10 minutos ('bloquear' para cerrarla antes).");
                encolarEvento("consola_desbloqueada", "");
            } else {
                g_frenoConsola.registrarFallo(millis());
                Serial.println("Clave de servicio incorrecta.");
                if (g_frenoConsola.fallos() == core::FrenoIntentos::MAX_INTENTOS) {
                    core::ObjetoJSON d;
                    d.sinSigno("intentos", g_frenoConsola.fallos());
                    encolarEvento("consola_desbloqueo_fallido", d.cerrar());
                }
            }
        } else if (verbo == "bloquear") {
            g_consola.bloquear();
            Serial.println(hayClave ? "Consola bloqueada." : "Sin clave de servicio: no hay bloqueo que aplicar.");
        } else if (verbo == "estado") {
            imprimirEstado();
        } else if (verbo == "rssi") {
            imprimirRssi();
        } else if (verbo == "reiniciar") {
            Serial.println("Reiniciando (DISCONNECT ordenado)...");
            // IOT-002: el DISCONNECT ordenado no dispara el LWT; el reinicio
            // manual se informa antes para que no pase desapercibido.
            if (mqtt.conectado()) publicarEventoDirecto("reinicio_manual", "");
            mqtt.desconectarOrdenadamente();  // HU-13 esc. 2: no dispara el LWT
            delay(200);
            ESP.restart();
        } else if (verbo == "nvs") {
            const int e2 = resto.indexOf(' ');
            const String accion = e2 < 0 ? resto : resto.substring(0, e2);
            String args = e2 < 0 ? String() : resto.substring(e2 + 1);
            args.trim();
            if (accion == "set") {
                const int e3 = args.indexOf(' ');
                const String clave = e3 < 0 ? args : args.substring(0, e3);
                const String valor = e3 < 0 ? String() : args.substring(e3 + 1);
                const bool ok = guardarClaveNvs(clave, valor);
                // Nunca se hace eco del valor: el monitor serie acaba en anexos.
                Serial.printf("nvs set %s -> %s (valor no mostrado). Reiniciar para aplicar", clave.c_str(),
                              ok ? "OK" : "RECHAZADO");
                Serial.println(clave == "mqtt_token" ? "; el token se aplica sin reiniciar." : ".");
                if (ok && clave == "mqtt_token") g_revisarTokenYa = true;
                if (ok) {
                    if (clave == "clave_servicio") g_huellaServicio = huellaClaveServicio();
                    // IOT-002: el cambio se informa (qué clave, nunca el valor).
                    core::ObjetoJSON d;
                    d.cadena("clave", std::string(clave.c_str()));
                    d.cadena("accion", "set");
                    encolarEvento("config_modificada", d.cerrar());
                }
            } else if (accion == "borrar") {
                const bool ok = borrarClaveNvs(args);
                Serial.printf("nvs borrar %s -> %s\n", args.c_str(), ok ? "OK" : "RECHAZADO");
                if (ok) {
                    if (args == "clave_servicio") g_huellaServicio = String();
                    core::ObjetoJSON d;
                    d.cadena("clave", std::string(args.c_str()));
                    d.cadena("accion", "borrar");
                    encolarEvento("config_modificada", d.cerrar());
                }
            } else {
                Serial.println("Uso: nvs set <clave> <valor> | nvs borrar <clave>");
            }
#if MODO_DEMO
        } else if (verbo == "demo") {
            // demo excursion 12.5 4
            float temp = 12.5f;
            long n = 4;
            if (sscanf(resto.c_str(), "excursion %f %ld", &temp, &n) >= 1 && n > 0 && n <= 20 &&
                temp >= -40.0f && temp <= 60.0f) {
                g_demoTempCenti = (int16_t)(temp * 100.0f);
                g_demoRestantes = (uint32_t)n;
                Serial.printf("[DEMO] %.2f C SIMULADOS en las proximas %ld lecturas.\n", temp, n);
            } else {
                Serial.println("Uso: demo excursion <temp_C> <n_lecturas 1..20>");
            }
#endif
        } else {
            Serial.printf("Comando desconocido: '%s' (escribir 'ayuda').\n", verbo.c_str());
        }
    }
}

// =========================================================================
// Red
// =========================================================================
static void publicarEventoDirecto(const char* tipo, const std::string& detalle) {
    const std::string json = core::serializarEvento(std::string(g_config.deviceId.c_str()), g_bootId,
                                                    tipo, epochAhora(), uptimeSegundos(),
                                                    baseTiempo(), detalle);
    if (mqtt.publicar(mqtt.topicoEventos(), json) != MQTTManager::Publicacion::Confirmada) {
        LOG_E("Red", "El broker no confirmo el evento '%s'.", tipo);
    }
}

/// HU-06 esc. 3: ninguna pérdida queda silenciosa. Aviso al cruzar el 80 %
/// y reporte del episodio de saturación con su periodo.
static void revisarAvisosBuffer() {
    uint8_t pct = 0;
    if (buffer.consumirAvisoOcupacion(pct)) {
        core::ObjetoJSON d;
        d.sinSigno("ocupacion_pct", pct);
        d.sinSigno("pendientes", buffer.pendientes());
        d.sinSigno("capacidad", buffer.capacidad());
        encolarEvento("buffer_80", d.cerrar());
        LOG_E("Buffer", "Ocupacion %u%% (umbral de aviso %u%%).", pct, BUFFER_UMBRAL_AVISO_PCT);
    }
    // El estado de pérdidas cambia con CADA muestra no almacenada. Se reporta
    // al abrir el episodio, al cerrarlo y, mientras siga abierto, como mucho
    // cada 10 min: si no, un nodo saturado inundaría la cola de eventos.
    static bool reportadoAbierto = false;
    static uint32_t ultimoReporteMs = 0;
    core::EstadoPerdidas p;
    if (buffer.consumirAvisoPerdidas(p)) {
        if (p.episodioAbierto && reportadoAbierto && millis() - ultimoReporteMs < 600000UL) return;
        reportadoAbierto = p.episodioAbierto;
        ultimoReporteMs = millis();
        core::ObjetoJSON d;
        d.sinSigno("perdidas_total", p.total);
        d.sinSigno("perdidas_episodio", p.episodio);
        d.booleano("episodio_abierto", p.episodioAbierto);
        d.sinSigno("desde_boot_id", p.desdeBoot);
        d.sinSigno("desde_seq_no", p.desdeSeq);
        if (p.desdeEpoch) d.cadena("desde", core::formatearISO8601(p.desdeEpoch));
        else              d.nulo("desde");
        d.sinSigno("hasta_boot_id", p.hastaBoot);
        d.sinSigno("hasta_seq_no", p.hastaSeq);
        if (p.hastaEpoch) d.cadena("hasta", core::formatearISO8601(p.hastaEpoch));
        else              d.nulo("hasta");
        d.cadena("causa", "buffer_lleno");
        encolarEvento("buffer_saturado", d.cerrar());
        LOG_E("Buffer", "Muestras no almacenadas: total=%lu, episodio=%lu (%s), seq %lu a %lu.",
              (unsigned long)p.total, (unsigned long)p.episodio, p.episodioAbierto ? "abierto" : "cerrado",
              (unsigned long)p.desdeSeq, (unsigned long)p.hastaSeq);
    }
}

/// HU-48: latido cada 5 min con el estado del buffer (datos pendientes,
/// pérdidas) y del enlace, para que el dashboard muestre la completitud del
/// registro. También sirve de evidencia de RSSI para el protocolo C-5.
static void publicarEstadoNodo(uint32_t pubacksSinAck) {
    core::EstadoPerdidas p = buffer.perdidas();
    core::ObjetoJSON d;
    d.sinSigno("pendientes", buffer.pendientes());
    d.sinSigno("ocupacion_pct", buffer.ocupacionPct());
    d.sinSigno("capacidad", buffer.capacidad());
    d.sinSigno("perdidas_total", p.total);
    d.sinSigno("cuarentena", buffer.cuarentenados());
    d.sinSigno("ultimo_seq_no", g_seqNo);
    d.entero("rssi_dbm", wifi.rssi());
    d.sinSigno("heap_libre", ESP.getFreeHeap());
    d.sinSigno("heap_minimo", ESP.getMinFreeHeap());
    d.sinSigno("intervalo_s", g_config.intervaloMuestreoS);
    d.sinSigno("pubacks_sin_ack", pubacksSinAck);
    d.sinSigno("eventos_descartados", eventosDescartados());
    d.booleano("reloj_sntp", relojSincronizado());
    publicarEventoDirecto("estado_nodo", d.cerrar());
}

static void publicarEventosPendientes() {
    EventoPendiente e;
    int publicados = 0;
    while (publicados < 5 && tomarEvento(e)) {
        const std::string json = core::serializarEvento(
            std::string(g_config.deviceId.c_str()), g_bootId, e.tipo.c_str(), e.epoch, e.uptimeS,
            baseTiempo(), e.detalleJSON);
        alimentarWatchdog();
        if (mqtt.publicar(mqtt.topicoEventos(), json) != MQTTManager::Publicacion::Confirmada) {
            devolverEvento(e);  // se reintenta en la próxima pasada
            return;
        }
        publicados++;
    }
}

/// HU-44 esc. 2: relee el token de NVS; si cambió (rotación o revocación
/// aplicada por el administrador), cierra con DISCONNECT ordenado y reconecta
/// con la credencial nueva SIN reiniciar: boot_id, seq_no y buffer siguen.
static void revisarRotacionToken() {
    if (g_config.identidadBloqueada) return;  // IOT-005: nunca se entrega un token
    const String nuevo = releerTokenMqtt();
    if (nuevo == mqtt.token() || nuevo.isEmpty()) return;
    LOG_I("Red", "Token MQTT rotado en NVS: reconexion ordenada sin reiniciar (HU-44).");
    mqtt.desconectarOrdenadamente();  // DISCONNECT limpio: no dispara el LWT
    mqtt.actualizarToken(nuevo);
    encolarEvento("credencial_rotada", "");
}

static void taskRed(void*) {
    suscribirTareaAlWatchdog("Red");
    static TransmisorMQTT transmisor(mqtt, g_config.deviceId);
    core::Backoff backoffDrenaje(DRENAJE_REINTENTO_BASE_MS, DRENAJE_REINTENTO_MAX_MS, 2);
    uint32_t ultimoDrenajeMs = 0;
    bool drenajeEnEspera = false;
    bool sesionAnunciada = false;
    uint32_t ultimaRevisionTokenMs = millis();
    uint32_t ultimoEstadoMs = millis();
    wifi.configurar(g_config.wifiSsid, g_config.wifiPassword);
    wifi.begin();

    for (;;) {
        alimentarWatchdog();
        atenderConsola();
        // Sin red también: el aviso del 80 % y la saturación quedan en el log
        // local y encolados; se publican al volver la conexión.
        revisarAvisosBuffer();

        uint32_t epochSync = 0;
        if (consumirAvisoSincronizacion(epochSync)) {
            LOG_I("Reloj", "SNTP sincronizado: %s", core::formatearISO8601(epochSync).c_str());
        }

        // HU-08: backoff exponencial 1 s → 60 s; la captura sigue en la
        // otra tarea aunque la red no vuelva nunca.
        if (!wifi.mantener()) {
            sesionAnunciada = false;
            delay(200);
            continue;
        }
        arrancarSNTP();

        // HU-44: rotación/revocación del token sin reiniciar. Va ANTES de
        // conectar: un nodo rechazado por credencial revocada debe poder
        // recoger el token nuevo sin tener una sesión activa.
        if (g_revisarTokenYa || millis() - ultimaRevisionTokenMs >= 300000UL) {
            g_revisarTokenYa = false;
            ultimaRevisionTokenMs = millis();
            revisarRotacionToken();
        }

        // HU-09: canal MQTT sobre TLS con la CA validada. Sin CA o sin
        // credenciales, fallo cerrado: no hay alternativa en texto plano.
        if (!mqtt.conectado()) {
            sesionAnunciada = false;
            const bool ok = mqtt.conectar();
            alimentarWatchdog();  // handshake TLS + CONNACK: el tramo más largo
            if (!ok) {
                delay(500);
                continue;
            }
        }
        if (!sesionAnunciada) {
            // HU-13 esc. 3: el nodo anuncia que volvió a estar en línea.
            core::ObjetoJSON d;
            d.entero("rssi_dbm", wifi.rssi());
            d.sinSigno("pendientes", buffer.pendientes());
            d.cadena("firmware_version", core::textoVersionFirmware(FW_MAYOR, FW_MENOR, FW_PARCHE));
            publicarEventoDirecto("online", d.cerrar());
            // HU-08 esc. 3: el episodio de diagnóstico solo puede reportarse
            // ahora, con la red de vuelta.
            WiFiManager::Recuperacion rec;
            if (wifi.consumirRecuperacion(rec)) {
                core::ObjetoJSON dr;
                dr.sinSigno("intentos_fallidos", rec.intentos);
                dr.sinSigno("duracion_s", rec.duracionS);
                dr.sinSigno("umbral_intentos", WIFI_UMBRAL_DIAGNOSTICO_INTENTOS);
                encolarEvento("wifi_reconexion_prolongada", dr.cerrar());
            }
            backoffDrenaje.registrarExito();
            drenajeEnEspera = false;
            sesionAnunciada = true;
        }
        mqtt.loop();

        publicarEventosPendientes();

        // HU-07: drenaje FIFO con acuse lógico. Si el backend no acusa, se
        // espera con backoff para no republicar en ráfaga.
        if (!drenajeEnEspera || backoffDrenaje.debeIntentar(millis(), ultimoDrenajeMs)) {
            ultimoDrenajeMs = millis();
            const core::ResumenDrenaje r = buffer.drenar(transmisor, DRENAJE_MAX_POR_CICLO);
            if (r.detenido) {
                if (drenajeEnEspera) backoffDrenaje.registrarFallo();
                drenajeEnEspera = true;
            } else {
                backoffDrenaje.registrarExito();
                drenajeEnEspera = false;
            }
            if (r.corruptos + r.rechazados + r.invalidos > 0) {
                core::ObjetoJSON d;
                d.sinSigno("corruptos", (uint32_t)r.corruptos);
                d.sinSigno("rechazados_backend", (uint32_t)r.rechazados);
                d.sinSigno("invalidos", (uint32_t)r.invalidos);
                d.sinSigno("cuarentena_total", buffer.cuarentenados());
                encolarEvento("registros_en_cuarentena", d.cerrar());
            }
            if (r.confirmados > 0) {
                LOG_I("Red", "%d lectura(s) con acuse logico; quedan %lu.", r.confirmados,
                      (unsigned long)buffer.pendientes());
            }
        }

        // HU-48: latido de estado (pendientes, pérdidas, enlace).
        if (millis() - ultimoEstadoMs >= INTERVALO_ESTADO_MS) {
            ultimoEstadoMs = millis();
            publicarEstadoNodo(transmisor.pubacksSinAck());
        }
        delay(50);
    }
}

// =========================================================================
// Arranque
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(300);
    const esp_reset_reason_t motivo = esp_reset_reason();

    inicializarWatchdog();
    iniciarEventos();
    g_config = cargarConfiguracion();
    g_bootId = cargarEIncrementarBootId();
    iniciarReloj(g_bootId);

    // Línea fija de arranque: la usa analizar_ensayo.py para contar reinicios.
    Serial.printf("[BOOT] boot_id=%lu reset=%s fw=v%d.%d.%d device=%s intervalo_s=%lu mc38=%d "
                  "sensado_energia=%d demo=%d credenciales=%s\n",
                  (unsigned long)g_bootId, nombreReinicio(motivo), FW_MAYOR, FW_MENOR, FW_PARCHE,
                  g_config.deviceId.c_str(), (unsigned long)g_config.intervaloMuestreoS,
                  MC38_INSTALADO, SENSADO_ENERGIA_INSTALADO, MODO_DEMO, g_config.origen.c_str());

    bool formateado = false;
    core::InformeMontaje informe;
    if (!buffer.begin(formateado, informe)) {
        LOG_E("Setup", "LittleFS inutilizable: cada lectura se contara como perdida.");
    } else if (formateado) {
        LOG_E("Setup", "LittleFS no montaba y se formateo: buffer vacio.");
    }

    // IOT-005: con la identidad bloqueada no se entrega token al cliente MQTT:
    // queda "no aprovisionado" (fallo cerrado) y las lecturas siguen en LittleFS.
    // IOT-002: la consola de escritura se bloquea en un nodo aprovisionado
    // con clave de servicio.
    g_huellaServicio = huellaClaveServicio();
    g_aprovisionadoAlArrancar = g_config.credencialesCompletas();
    if (g_aprovisionadoAlArrancar && g_huellaServicio.isEmpty()) {
        LOG_E("Setup", "Consola de escritura ABIERTA: defina 'nvs set clave_servicio <clave>' para "
                       "bloquearla en este nodo aprovisionado.");
    }

    mqtt.configurar(g_config.mqttHost, g_config.deviceId,
                    g_config.identidadBloqueada ? String() : g_config.mqttToken);
    if (g_config.identidadBloqueada) {
        LOG_E("Setup", "FIRMWARE DEMO sobre un nodo con device_id productivo en NVS: NO se publica. "
                       "Instale el firmware de produccion o use una placa de demostracion.");
    } else if (!g_config.credencialesCompletas()) {
        LOG_E("Setup", "Nodo SIN aprovisionar: captura y guarda en LittleFS, pero no publica. "
                       "Ver 'ayuda' en esta consola.");
    }

    iniciarEnergia();
    ds18b20.begin();
    sht31.begin();
#if MC38_INSTALADO
    mc38.begin();
#endif
#if MODO_DEMO
    pinMode(PIN_BOTON_DEMO, INPUT_PULLUP);
    LOG_I("DEMO", "FIRMWARE DE DEMOSTRACION: mantener BOOT 1 s = excursion simulada.");
#endif

    {
        core::ObjetoJSON d;
        d.cadena("reset_reason", nombreReinicio(motivo));
        d.cadena("firmware_version", core::textoVersionFirmware(FW_MAYOR, FW_MENOR, FW_PARCHE));
        d.sinSigno("intervalo_s", g_config.intervaloMuestreoS);
        d.booleano("mc38_instalado", MC38_INSTALADO != 0);
        d.booleano("sensado_energia", SENSADO_ENERGIA_INSTALADO != 0);
        d.booleano("fs_formateado", formateado);
        d.booleano("consola_protegida", g_aprovisionadoAlArrancar && !g_huellaServicio.isEmpty());
        d.sinSigno("pendientes_recuperados", informe.pendientes);
        d.sinSigno("segmentos_con_cola_parcial", informe.segmentosConColaParcial);
        d.sinSigno("bytes_parciales_ignorados", informe.bytesParcialesIgnorados);
        d.booleano("cursor_reconstruido", informe.cursorReconstruido);
        encolarEvento("arranque", d.cerrar());
    }

    xTaskCreatePinnedToCore(taskCaptura, "Captura", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(taskRed, "Red", 16384, nullptr, 2, nullptr, 1);
}

void loop() {
    // Todo corre en las tareas. loopTask no está suscrita al watchdog.
    delay(1000);
}
