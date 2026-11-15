#ifndef CORE_RECONEXION_H
#define CORE_RECONEXION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Tiempo.h"

namespace core {

/**
 * Episodio de reconexión del nodo (HU-08 esc. 3, H1 del banco).
 *
 * Un episodio empieza con el primer intento FALLIDO de recuperar la sesión
 * MQTT (asociación Wi-Fi o conexión al broker) y termina cuando el nodo vuelve
 * a conectar con el broker. Cuenta los intentos fallidos, desde cuándo dura y
 * cuántas veces reinició el nodo en medio.
 *
 * Antes vivía solo en RAM (WiFiManager): un reinicio por watchdog en pleno
 * episodio borraba el contador y la hora de inicio, y el diagnóstico que pide
 * el criterio ("se registra el estado localmente") desaparecía justo cuando
 * hacía falta. Ahora se persiste (NVS en el ESP32) con magia y CRC32:
 *   - al abrir el episodio, al cruzar el umbral de diagnóstico, al cerrarlo y
 *     al contar un reinicio;
 *   - y, entre medias, como mucho una vez por `intervaloPersistenciaS` de
 *     uptime, para no escribir la flash en cada reintento.
 * Un reinicio pierde a lo sumo los intentos de ese último intervalo; nunca el
 * inicio del episodio.
 *
 * El último episodio cerrado se conserva hasta que empieza otro: viaja en cada
 * `estado_nodo` y, si llegó al umbral o hubo reinicios, en el evento
 * `wifi_reconexion_prolongada` al reconectar.
 */

enum class EtapaReconexion : uint8_t { Ninguna = 0, WiFi = 1, Broker = 2 };

/// "wifi" / "broker"; nullptr para `Ninguna`.
const char* nombreEtapaReconexion(EtapaReconexion etapa);

/// Instante del nodo: arranque, uptime y época (0 sin SNTP en ese arranque).
struct MarcaTiempo {
    uint32_t bootId = 0;
    uint32_t uptimeS = 0;
    uint32_t epoch = 0;
};

struct EpisodioReconexion {
    bool activo = false;
    bool diagnostico = false;  ///< superó el umbral de intentos
    EtapaReconexion etapa = EtapaReconexion::Ninguna;  ///< etapa del último fallo
    uint32_t intentos = 0;     ///< intentos fallidos del episodio
    uint32_t reinicios = 0;    ///< reinicios del nodo con el episodio abierto
    MarcaTiempo desde;
    MarcaTiempo hasta;         ///< solo con el episodio cerrado
};

static const size_t TAM_EPISODIO_RECONEXION = 44;

void codificarEpisodio(const EpisodioReconexion& e, uint8_t salida[TAM_EPISODIO_RECONEXION]);
/// false si la magia, la versión o el CRC no coinciden.
bool decodificarEpisodio(const uint8_t entrada[TAM_EPISODIO_RECONEXION], EpisodioReconexion& e);

/// Almacén de los bytes del episodio (NVS en el ESP32).
class PersistenciaReconexion {
public:
    virtual ~PersistenciaReconexion() = default;
    virtual bool leer(uint8_t* datos, size_t longitud) = 0;
    virtual bool escribir(const uint8_t* datos, size_t longitud) = 0;
};

class SeguimientoReconexion {
public:
    SeguimientoReconexion(PersistenciaReconexion& persistencia, uint32_t umbralIntentos,
                          uint32_t intervaloPersistenciaS = 60);

    /// Al arrancar: recupera el episodio guardado. Si seguía abierto, el nodo
    /// reinició en medio: se cuenta y se guarda de inmediato.
    void montar(uint32_t bootId);

    /// La sesión MQTT se perdió en `ahora`: si el próximo intento falla, el
    /// episodio empieza aquí y no en ese intento.
    void marcarDesconexion(const MarcaTiempo& ahora);

    /// Un intento de conexión falló. Devuelve true si este fallo cruza el
    /// umbral de diagnóstico.
    bool registrarFallo(EtapaReconexion etapa, const MarcaTiempo& ahora);

    /// Sesión MQTT restablecida. Cierra el episodio abierto y devuelve true si
    /// hay que reportarlo (llegó al umbral o el nodo reinició en medio).
    bool registrarConexion(const MarcaTiempo& ahora, EpisodioReconexion& cerrado);

    /// Episodio en curso o, si no hay, el último cerrado.
    const EpisodioReconexion& episodio() const { return _episodio; }
    uint32_t umbral() const { return _umbral; }
    uint32_t escrituras() const { return _escrituras; }

private:
    void _guardar(const MarcaTiempo& ahora);

    PersistenciaReconexion& _persistencia;
    uint32_t _umbral;
    uint32_t _intervaloS;
    EpisodioReconexion _episodio;
    bool _hayDesconexion = false;
    MarcaTiempo _desconexion;
    bool _hayGuardado = false;
    MarcaTiempo _ultimoGuardado;
    uint32_t _escrituras = 0;
};

/// Época de una marca: la propia, o derivada del uptime si es de este
/// arranque y ya hay SNTP. false si no se puede fechar sin inventar.
bool epochDeMarca(const MarcaTiempo& m, const BaseTiempo& base, uint32_t& epoch);

/// Duración del episodio en segundos. false si sus extremos no se pueden
/// comparar (distintos arranques sin hora).
bool duracionEpisodioS(const EpisodioReconexion& e, const BaseTiempo& base, uint32_t& duracionS);

/// `detail` del evento `wifi_reconexion_prolongada` (contrato v1.0):
/// `intentos_fallidos`, `duracion_s`, `umbral_intentos` y, desde la v1.2.1,
/// `desde`, `hasta`, `reinicios`, `etapa`.
std::string detalleReconexionProlongada(const EpisodioReconexion& e, uint32_t umbral, const BaseTiempo& base);

}  // namespace core

#endif  // CORE_RECONEXION_H
