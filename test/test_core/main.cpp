/**
 * Pruebas unitarias del host (`pio test -e native`).
 *
 * Todo `src/core/` es C++ portable sin Arduino y se verifica aquí sin
 * hardware y en cada push del CI.
 */
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void run_tests_sensores(void);
void run_tests_tiempo(void);
void run_tests_contrato(void);
void run_tests_ack(void);
void run_tests_cola(void);
void run_tests_antirrebote(void);
void run_tests_backoff(void);
void run_tests_credenciales(void);
void run_tests_identidad(void);
void run_tests_consola(void);
void run_tests_ota(void);
void run_tests_pem(void);
void run_tests_posesion(void);
void run_tests_llenado_banco(void);

int main(int, char**) {
    UNITY_BEGIN();
    run_tests_sensores();
    run_tests_tiempo();
    run_tests_contrato();
    run_tests_ack();
    run_tests_cola();
    run_tests_antirrebote();
    run_tests_backoff();
    run_tests_credenciales();
    run_tests_identidad();
    run_tests_consola();
    run_tests_ota();
    run_tests_pem();
    run_tests_posesion();
    run_tests_llenado_banco();
    return UNITY_END();
}
