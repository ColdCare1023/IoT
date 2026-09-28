#include "PowerBackup.h"

#include <Arduino.h>

#include "../config.h"
#include "../core/Antirrebote.h"
#include "../core/Json.h"
#include "Eventos.h"
#include "Reloj.h"

#if SENSADO_ENERGIA_INSTALADO

// Antirrebote de 500 ms: el contacto del conmutador del UPS y el arranque del
// adaptador producen transitorios; un corte real dura mucho más.
static core::Antirrebote _sin_red(500);
static bool _enRespaldo = false;
static uint32_t _inicioRespaldoS = 0;
static bool _avisoAutonomiaEnviado = false;

void iniciarEnergia() {
    pinMode(PIN_SENSADO_ENERGIA, INPUT);  // el divisor fija el nivel: sin pull interno
    const bool sinRed = digitalRead(PIN_SENSADO_ENERGIA) == LOW;
    _sin_red.inicializar(sinRed, (uint32_t)millis());
    _enRespaldo = sinRed;
    if (sinRed) _inicioRespaldoS = uptimeSegundos();
    LOG_I("Energia", "Sensado en GPIO%d: %s.", PIN_SENSADO_ENERGIA,
          sinRed ? "ARRANQUE CON RESPALDO" : "red comercial");
}

void muestrearEnergia() {
    const bool sinRed = digitalRead(PIN_SENSADO_ENERGIA) == LOW;
    if (_sin_red.muestrear(sinRed, (uint32_t)millis())) {
        _enRespaldo = _sin_red.estaAbierta();
        if (_enRespaldo) {
            _inicioRespaldoS = uptimeSegundos();
            _avisoAutonomiaEnviado = false;
            core::ObjetoJSON d;
            d.cadena("desde", "mains");
            d.cadena("hacia", "backup");
            encolarEvento("conmutacion_respaldo", d.cerrar());
            LOG_E("Energia", "Corte de suministro: nodo con respaldo 5 V.");
        } else {
            core::ObjetoJSON d;
            d.sinSigno("duracion_respaldo_s", uptimeSegundos() - _inicioRespaldoS);
            encolarEvento("suministro_restablecido", d.cerrar());
            LOG_I("Energia", "Suministro comercial restablecido.");
        }
    }

#if AUTONOMIA_RESPALDO_MEDIDA_MIN > 0
    if (_enRespaldo && !_avisoAutonomiaEnviado) {
        const uint32_t transcurridoS = uptimeSegundos() - _inicioRespaldoS;
        const uint32_t umbralS = (uint32_t)AUTONOMIA_RESPALDO_MEDIDA_MIN * 60u * AVISO_AUTONOMIA_PCT / 100u;
        if (transcurridoS >= umbralS) {
            _avisoAutonomiaEnviado = true;
            core::ObjetoJSON d;
            d.sinSigno("transcurrido_s", transcurridoS);
            d.sinSigno("autonomia_medida_min", AUTONOMIA_RESPALDO_MEDIDA_MIN);
            d.cadena("base", "estimacion_por_ensayo_C6");
            encolarEvento("respaldo_autonomia_critica", d.cerrar());
        }
    }
#endif
}

bool energiaSensada() { return true; }
bool enRespaldo() { return _enRespaldo; }

#else  // sin sensado: nada que leer, ningún pin tocado

void iniciarEnergia() {
    LOG_I("Energia", "Sin sensado de suministro (continuidad por secuencia boot_id/seq_no).");
}
void muestrearEnergia() {}
bool energiaSensada() { return false; }
bool enRespaldo() { return false; }

#endif
