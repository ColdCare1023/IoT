#ifndef SYSTEM_DIAGNOSTICO_RECONEXION_H
#define SYSTEM_DIAGNOSTICO_RECONEXION_H

#include "../core/Reconexion.h"

/**
 * Episodio de reconexión persistido en NVS (HU-08 esc. 3, H1 del banco).
 *
 * La política (cuándo empieza, cuándo se guarda, cuándo se reporta) es
 * `core::SeguimientoReconexion`, probada en el host. Aquí solo está el acceso
 * a NVS (clave `reconexion`, blob de 44 B con CRC32). Lo usa únicamente la
 * tarea de red (y `setup()` antes de crearla), así que no lleva cerrojo.
 */
class ReconexionNVS : public core::PersistenciaReconexion {
public:
    bool leer(uint8_t* datos, size_t longitud) override;
    bool escribir(const uint8_t* datos, size_t longitud) override;
};

core::SeguimientoReconexion& diagnosticoReconexion();

/// Instante actual del nodo (arranque, uptime y época si hay SNTP).
core::MarcaTiempo marcaAhora(uint32_t bootId);

#endif  // SYSTEM_DIAGNOSTICO_RECONEXION_H
