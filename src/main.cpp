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
 *   Red     (prioridad 2) — Wi-Fi con backoff exponencial, SNTP y consola
 *       serie de aprovisionamiento.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "core/Registro.h"
#include "core/Tiempo.h"
#include "network/WiFiManager.h"
#include "sensors/DS18B20Sensor.h"
#include "sensors/MC38Sensor.h"
#include "sensors/SHT31Sensor.h"
#include "storage/BufferLittleFS.h"
#include "system/Configuracion.h"
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
static WiFiManager wifi;

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

    r.fwMayor = FW_MAYOR;
    r.fwMenor = FW_MENOR;
    r.fwParche = FW_PARCHE;
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
                  "door=%s door_s=%u g=%s q=%lu ocup=%u heap=%lu\n",
                  (unsigned long)r.bootId, (unsigned long)r.seqNo, (unsigned long)r.uptimeS, t.c_str(),
                  r.tiene(core::BANDERA_TIEMPO_SNTP) ? "sntp" : "nosync", ds, dsF ? dsF : "-", sht,
                  hr, shF ? shF : "-", puerta, (unsigned)r.puertaAbiertaS, textoGuardado(g),
                  (unsigned long)buffer.pendientes(), (unsigned)buffer.ocupacionPct(),
                  (unsigned long)ESP.getFreeHeap());
}

/// HU-06 esc. 3: ninguna pérdida queda silenciosa. Aviso al cruzar el 80 %
/// y reporte del episodio de saturación con su periodo.
static void revisarAvisosBuffer() {
    uint8_t pct = 0;
    if (buffer.consumirAvisoOcupacion(pct)) {
        LOG_E("Buffer", "Ocupacion %u%% (umbral de aviso %u%%): %lu pendientes de %lu.", pct,
              BUFFER_UMBRAL_AVISO_PCT, (unsigned long)buffer.pendientes(),
              (unsigned long)buffer.capacidad());
    }
    core::EstadoPerdidas p;
    if (buffer.consumirAvisoPerdidas(p)) {
        LOG_E("Buffer", "Muestras no almacenadas: total=%lu, episodio=%lu (%s) desde seq %lu hasta seq %lu.",
              (unsigned long)p.total, (unsigned long)p.episodio, p.episodioAbierto ? "abierto" : "cerrado",
              (unsigned long)p.desdeSeq, (unsigned long)p.hastaSeq);
    }
}

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
        revisarAvisosBuffer();

        // Espera hasta el próximo ciclo muestreando la puerta cada 50 ms.
        const TickType_t intervalo = pdMS_TO_TICKS(g_config.intervaloMuestreoS * 1000UL);
        for (;;) {
            const int32_t faltan = (int32_t)(ultimo + intervalo - xTaskGetTickCount());
            if (faltan <= (int32_t)paso) break;
            vTaskDelay(paso);
#if MC38_INSTALADO
            mc38.poll();  // antirrebote de 50 ms: requiere muestras frecuentes
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
    Serial.printf("wifi=%s rssi=%d dBm intentos_fallidos=%lu retardo=%lums diag=%d\n",
                  wifi.conectado() ? "OK" : "NO", wifi.rssi(), (unsigned long)wifi.intentosFallidos(),
                  (unsigned long)wifi.retardoActualMs(), wifi.diagnosticoActivo() ? 1 : 0);
    Serial.printf("reloj=%s | buffer: pendientes=%lu/%lu ocupacion=%u%% libres=%luKB cuarentena=%lu\n",
                  relojSincronizado() ? "sntp_synced" : "unsynced", (unsigned long)buffer.pendientes(),
                  (unsigned long)buffer.capacidad(), (unsigned)buffer.ocupacionPct(),
                  (unsigned long)(buffer.bytesLibres() / 1024), (unsigned long)buffer.cuarentenados());
    Serial.printf("perdidas: total=%lu episodio=%lu abierto=%d | heap=%lu (min %lu)\n",
                  (unsigned long)p.total, (unsigned long)p.episodio, p.episodioAbierto ? 1 : 0,
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
                  WiFi.SSID().c_str(), WiFi.channel(), promedio, minimo, maximo, n,
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

        if (verbo == "ayuda" || verbo == "help") {
            Serial.println("Comandos: estado | rssi | nvs set <clave> <valor> | nvs borrar <clave> | reiniciar");
            Serial.println("Claves NVS: wifi_ssid wifi_pass mqtt_host mqtt_token intervalo_s");
        } else if (verbo == "estado") {
            imprimirEstado();
        } else if (verbo == "rssi") {
            imprimirRssi();
        } else if (verbo == "reiniciar") {
            Serial.println("Reiniciando...");
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
                Serial.printf("nvs set %s -> %s (valor no mostrado). Reiniciar para aplicar.\n",
                              clave.c_str(), ok ? "OK" : "RECHAZADO");
            } else if (accion == "borrar") {
                Serial.printf("nvs borrar %s -> %s\n", args.c_str(), borrarClaveNvs(args) ? "OK" : "RECHAZADO");
            } else {
                Serial.println("Uso: nvs set <clave> <valor> | nvs borrar <clave>");
            }
        } else {
            Serial.printf("Comando desconocido: '%s' (escribir 'ayuda').\n", verbo.c_str());
        }
    }
}

// =========================================================================
// Red
// =========================================================================
static void taskRed(void*) {
    suscribirTareaAlWatchdog("Red");
    wifi.configurar(g_config.wifiSsid, g_config.wifiPassword);
    wifi.begin();

    for (;;) {
        alimentarWatchdog();
        atenderConsola();

        uint32_t epochSync = 0;
        if (consumirAvisoSincronizacion(epochSync)) {
            LOG_I("Reloj", "SNTP sincronizado: %s", core::formatearISO8601(epochSync).c_str());
        }

        // HU-08: backoff exponencial 1 s → 60 s; la captura sigue en la
        // otra tarea aunque la red no vuelva nunca.
        if (!wifi.mantener()) {
            delay(200);
            continue;
        }
        arrancarSNTP();

        WiFiManager::Recuperacion rec;
        if (wifi.consumirRecuperacion(rec)) {
            LOG_I("WiFi", "Reconectado tras episodio de diagnostico: %lu intentos, %lu s sin red.",
                  (unsigned long)rec.intentos, (unsigned long)rec.duracionS);
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
    g_config = cargarConfiguracion();
    g_bootId = cargarEIncrementarBootId();
    iniciarReloj(g_bootId);

    // Línea fija de arranque: la usa analizar_ensayo.py para contar reinicios.
    Serial.printf("[BOOT] boot_id=%lu reset=%s fw=v%d.%d.%d device=%s intervalo_s=%lu mc38=%d "
                  "credenciales=%s\n",
                  (unsigned long)g_bootId, nombreReinicio(motivo), FW_MAYOR, FW_MENOR, FW_PARCHE,
                  g_config.deviceId.c_str(), (unsigned long)g_config.intervaloMuestreoS,
                  MC38_INSTALADO, g_config.origen.c_str());

    bool formateado = false;
    core::InformeMontaje informe;
    if (!buffer.begin(formateado, informe)) {
        LOG_E("Setup", "LittleFS inutilizable: cada lectura se contara como perdida.");
    } else if (formateado) {
        LOG_E("Setup", "LittleFS no montaba y se formateo: buffer vacio.");
    }

    ds18b20.begin();
    sht31.begin();
#if MC38_INSTALADO
    mc38.begin();
#endif

    xTaskCreatePinnedToCore(taskCaptura, "Captura", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(taskRed, "Red", 16384, nullptr, 2, nullptr, 1);
}

void loop() {
    // Todo corre en las tareas. loopTask no está suscrita al watchdog.
    delay(1000);
}
