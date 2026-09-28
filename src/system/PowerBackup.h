#ifndef SYSTEM_POWER_BACKUP_H
#define SYSTEM_POWER_BACKUP_H

#include <cstdint>

/**
 * HU-52 — continuidad ante corte de suministro (respaldo Mini UPS 5 V).
 *
 * CIRCUITO DE REFERENCIA (02-hardware-circuito/OE2.I2)
 * ---------------------------------------------------
 * Adaptador 5 V de pared → Mini UPS Forza 5 V → cable USB → VIN/USB del
 * ESP32. El respaldo alimenta SOLO al ESP32 y sus sensores; nunca al
 * refrigerador ni al router.
 *
 * SIN LECTURA ADC DE BATERÍA
 * --------------------------
 * La versión anterior leía GPIO 34 con analogRead() como "nivel de batería".
 * El Mini UPS no expone esa señal: el pin quedaba flotante y producía picos y
 * lecturas falsas. Se eliminó. La continuidad energética se DEMUESTRA con la
 * secuencia de captura: durante el corte no debe aparecer un boot_id nuevo ni
 * un salto de seq_no (lo verifica 03-qa-pruebas/scripts/analizar_ensayo.py).
 *
 * SENSADO DIGITAL OPCIONAL (SENSADO_ENERGIA_INSTALADO=1)
 * -----------------------------------------------------
 * Un divisor 10 kΩ/20 kΩ desde el 5 V del adaptador de pared (ANTES del Mini
 * UPS) lleva ~3,3 V a GPIO 27: HIGH = hay red comercial, LOW = el nodo corre
 * con el respaldo. Con esto cada lectura lleva power_source = "mains"/"backup"
 * y se registran los eventos de conmutación con su timestamp (HU-52 esc. 1).
 * El aviso de autonomía crítica (esc. 3) es una ESTIMACIÓN a partir de la
 * autonomía medida en el ensayo C-6, nunca una medición de batería.
 */

void iniciarEnergia();

/// Muestrea la entrada de sensado con antirrebote. Llamar cada ~50 ms desde
/// la tarea de captura; encola los eventos de transición.
void muestrearEnergia();

bool energiaSensada();
bool enRespaldo();

#endif  // SYSTEM_POWER_BACKUP_H
