#include "Watchdog.h"

#include <Arduino.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

// IOT-003: implementado para ESP-IDF 5.x (Arduino-ESP32 3.x, fijado en
// platformio.ini). La rama 4.4 está fuera de soporte; compilar con ella es un
// error explícito en vez de un comportamiento no verificado.
#if ESP_IDF_VERSION_MAJOR < 5
#error "TWDT: esta implementacion requiere ESP-IDF 5.x (Arduino-ESP32 3.x)."
#endif

void inicializarWatchdog() {
    // El core ya arranca el TWDT (CONFIG_ESP_TASK_WDT_INIT: 5 s, idle de la
    // CPU0, panic). En IDF 4.4 esp_task_wdt_init() sobre un TWDT ya activo
    // solo cambiaba plazo y panic; el equivalente en IDF 5 es reconfigurar,
    // conservando la vigilancia del idle de la CPU0 que fija el core.
    esp_task_wdt_config_t config = {};
    config.timeout_ms = WATCHDOG_TIMEOUT_S * 1000u;
    config.idle_core_mask = (1u << 0);
    config.trigger_panic = true;
    esp_err_t err = esp_task_wdt_reconfigure(&config);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&config);  // core sin TWDT iniciado
    if (err != ESP_OK) {
        LOG_E("WDT", "No se pudo configurar el watchdog (err=%d).", (int)err);
        return;
    }
    LOG_I("WDT", "Task watchdog a %u s.", WATCHDOG_TIMEOUT_S);
}

void suscribirTareaAlWatchdog(const char* nombreTarea) {
    const esp_err_t err = esp_task_wdt_add(nullptr);
    if (err != ESP_OK) {
        LOG_E("WDT", "No se pudo suscribir '%s' (err=%d).", nombreTarea, (int)err);
    }
}

void alimentarWatchdog() {
    esp_task_wdt_reset();  // ESP_ERR_NOT_FOUND si la tarea no está suscrita: se ignora
}
