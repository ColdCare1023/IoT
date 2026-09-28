#include "Watchdog.h"

#include <Arduino.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

// Implementado y verificado para ESP-IDF 4.4 (Arduino core 2.0.17, fijado en
// platformio.ini). IDF 5 cambió la firma de esp_task_wdt_init: fallo de
// compilación explícito en vez de un comportamiento no verificado.
#if ESP_IDF_VERSION_MAJOR >= 5
#error "TWDT: reescribir inicializarWatchdog() para IDF 5 (esp_task_wdt_config_t) y verificarlo."
#endif

void inicializarWatchdog() {
    const esp_err_t err = esp_task_wdt_init(WATCHDOG_TIMEOUT_S, /*panic=*/true);
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
