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
void run_tests_credenciales(void);
void run_tests_antirrebote(void);

int main(int, char**) {
    UNITY_BEGIN();
    run_tests_sensores();
    run_tests_tiempo();
    run_tests_contrato();
    run_tests_credenciales();
    run_tests_antirrebote();
    return UNITY_END();
}
