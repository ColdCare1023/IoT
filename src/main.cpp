/**
 * ThermoTrace Edge — firmware del nodo IoT (ESP32 DevKitC V4)
 * ============================================================
 * Tesis: Sistema IoT con IA y trazabilidad digital verificable para el
 * monitoreo de la cadena de frío de medicamentos termolábiles (Lima).
 * Capa Edge — responsable: Brenda Lucía Gamio Upiachihua.
 *
 * Tarea de captura en el núcleo 1 (el 0 queda para la pila Wi-Fi): cada
 * intervalo lee la sonda DS18B20 (interior) y el SHT31 (temperatura y humedad
 * del ambiente) y registra
 * la lectura identificada por boot_id y seq_no, con su hora UTC solo si es
 * confiable. Una falla del SHT31 nunca invalida la lectura del DS18B20.
 */

#include <Arduino.h>
#include <esp_system.h>

#include "config.h"
#include "core/Sensores.h"
#include "core/Tiempo.h"
#include "sensors/DS18B20Sensor.h"
#include "sensors/MC38Sensor.h"
#include "sensors/SHT31Sensor.h"
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

static void textoLectura(char* buf, size_t n, const core::LecturaSensor& l) {
    if (l.valida()) snprintf(buf, n, "%.2f", (double)l.valor);
    else            snprintf(buf, n, "NULL");
}

// =========================================================================
// Captura
// =========================================================================

/// Línea fija por captura. La consumen los scripts de QA (ensayo doméstico
/// de 2–3 h): no cambiar el formato sin actualizar analizar_ensayo.py.
/// Estado de la puerta en la ventana de reporte (HU-04).
struct VentanaPuerta {
    bool instalado = false;
    bool huboApertura = false;
    uint32_t abiertaS = 0;
    uint32_t rachaS = 0;  ///< solo si supera UMBRAL_PUERTA_PROLONGADA_S
};

static void imprimirCaptura(uint32_t seq, uint32_t uptimeS, uint32_t epoch,
                            const core::LecturaSensor& ds, const core::LecturaSHT31& sht,
                            const VentanaPuerta& puerta) {
    char dsTxt[16], shtTxt[16], hrTxt[16];
    textoLectura(dsTxt, sizeof(dsTxt), ds);
    textoLectura(shtTxt, sizeof(shtTxt), sht.temperatura);
    textoLectura(hrTxt, sizeof(hrTxt), sht.humedad);
    const char* dsF = core::codigoFallo(ds.fallo);
    const char* shF = core::codigoFallo(!sht.temperatura.valida() ? sht.temperatura.fallo
                                                                   : sht.humedad.fallo);
    const std::string t = epoch != 0 ? core::formatearISO8601(epoch) : std::string("-");
    const char* door = puerta.instalado ? (puerta.huboApertura ? "1" : "0") : "NA";
    Serial.printf("[CAP] boot=%lu seq=%lu up=%lu t=%s tq=%s ds=%s ds_f=%s sht=%s hr=%s sht_f=%s "
                  "door=%s door_s=%lu door_streak=%lu heap=%lu\n",
                  (unsigned long)g_bootId, (unsigned long)seq, (unsigned long)uptimeS, t.c_str(),
                  epoch != 0 ? "sntp" : "nosync", dsTxt, dsF ? dsF : "-", shtTxt, hrTxt,
                  shF ? shF : "-", door, (unsigned long)puerta.abiertaS,
                  (unsigned long)puerta.rachaS, (unsigned long)ESP.getFreeHeap());
}

static void taskCaptura(void*) {
    suscribirTareaAlWatchdog("Captura");
    TickType_t ultimo = xTaskGetTickCount();
    const TickType_t paso = pdMS_TO_TICKS(MC38_POLL_MS);

    for (;;) {
        const uint32_t seq = ++g_seqNo;
        const uint32_t uptime = uptimeSegundos();
        const uint32_t epoch = epochAhora();  // 0 si no hay SNTP: no se inventa la hora
        // Nunca un código de error como valor: una falla sale como
        // SENSOR_FAULT con su motivo (HU-01 esc. 2 y 3).
        const core::LecturaSensor ds = ds18b20.leer();
        alimentarWatchdog();
        const core::LecturaSHT31 sht = sht31.leer();  // independiente del DS18B20

        // HU-04: sin MC-38 instalado no se muestrea nada (un pin sin sensor
        // flota en HIGH = "abierta"): door_open se reporta como no aplicable.
        VentanaPuerta puerta;
#if MC38_INSTALADO
        puerta.instalado = true;
        puerta.huboApertura = mc38.huboApertura();
        puerta.abiertaS = mc38.segundosAbiertaEnVentana();
        const uint32_t racha = mc38.rachaSegundos();
        puerta.rachaS = racha >= UMBRAL_PUERTA_PROLONGADA_S ? racha : 0;
        mc38.cerrarVentana();
#endif
        imprimirCaptura(seq, uptime, epoch, ds, sht, puerta);

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
    Serial.printf("[BOOT] boot_id=%lu reset=%s fw=v%d.%d.%d device=%s intervalo_s=%lu\n",
                  (unsigned long)g_bootId, nombreReinicio(motivo), FW_MAYOR, FW_MENOR, FW_PARCHE,
                  g_config.deviceId.c_str(), (unsigned long)g_config.intervaloMuestreoS);

    ds18b20.begin();
    sht31.begin();
#if MC38_INSTALADO
    mc38.begin();
#endif

    xTaskCreatePinnedToCore(taskCaptura, "Captura", 8192, nullptr, 3, nullptr, 1);
}

void loop() {
    // Todo corre en las tareas. loopTask no está suscrita al watchdog.
    delay(1000);
}
