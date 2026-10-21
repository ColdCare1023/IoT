#ifndef CORE_CONSOLA_H
#define CORE_CONSOLA_H

#include <cstdint>
#include <string>

namespace core {

/**
 * IOT-002: política de la consola serie de aprovisionamiento.
 *
 * Antes cualquiera con un cable USB podía cambiar device_id, broker, token,
 * Wi-Fi o intervalo, borrar credenciales o reiniciar sin disparar el LWT.
 * Ahora, en un nodo aprovisionado con clave de servicio, la consola solo
 * admite comandos de lectura hasta `desbloquear <clave_servicio>`; la clave
 * se guarda únicamente como huella SHA-256 y los intentos fallidos se frenan.
 * Los cambios aceptados se publican como evento (ver main.cpp).
 */

/// ¿El comando modifica el nodo? (`estado`, `rssi`, `ayuda`, `desbloquear`
/// y `bloquear` no; todo lo demás sí, incluidos los desconocidos).
bool comandoModifica(const std::string& verbo);

/// Largo mínimo de una clave de servicio aceptable.
constexpr size_t CLAVE_SERVICIO_MIN = 12;
bool claveServicioAceptable(const std::string& clave);

/// Huella que se guarda en NVS (nunca la clave).
std::string huellaClaveServicio(const std::string& clave);

/// Compara la clave tecleada con la huella guardada en tiempo constante.
bool claveServicioCorrecta(const std::string& candidata, const std::string& huellaGuardada);

/// Freno de fuerza bruta: tras `MAX_INTENTOS` fallos seguidos, cada intento
/// adicional exige esperar `ESPERA_MS` desde el último fallo. Un acierto lo
/// reinicia. Sin dependencias de reloj: el llamador pasa `ahoraMs` (millis).
class FrenoIntentos {
public:
    static constexpr uint32_t MAX_INTENTOS = 5;
    static constexpr uint32_t ESPERA_MS = 10u * 60u * 1000u;

    bool puedeIntentar(uint32_t ahoraMs) const;
    void registrarFallo(uint32_t ahoraMs);
    void registrarAcierto();
    uint32_t fallos() const { return _fallos; }

private:
    uint32_t _fallos = 0;
    uint32_t _ultimoFalloMs = 0;
};

/// Estado de bloqueo. Solo se bloquea un nodo aprovisionado que tiene clave
/// de servicio; el desbloqueo dura `VENTANA_MS` desde `desbloquear`.
class EstadoConsola {
public:
    static constexpr uint32_t VENTANA_MS = 10u * 60u * 1000u;

    bool bloqueada(bool aprovisionado, bool hayClaveServicio, uint32_t ahoraMs) const;
    void desbloquear(uint32_t ahoraMs);
    void bloquear();

private:
    bool _desbloqueada = false;
    uint32_t _desdeMs = 0;
};

}  // namespace core

#endif  // CORE_CONSOLA_H
