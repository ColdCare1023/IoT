#include "Identidad.h"

#include "Payload.h"

namespace core {

Identidad resolverIdentidad(const std::string& deviceIdCompilado, const std::string& deviceIdNvs,
                            bool modoDemo) {
    Identidad identidad;
    identidad.deviceId = deviceIdCompilado;
    const bool hayIdNvs = deviceIdValido(deviceIdNvs);
    if (!modoDemo) {
        if (hayIdNvs) identidad.deviceId = deviceIdNvs;
        return identidad;
    }
    identidad.bloqueada = hayIdNvs && deviceIdNvs != deviceIdCompilado;
    return identidad;
}

}  // namespace core
