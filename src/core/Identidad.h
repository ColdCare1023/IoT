#ifndef CORE_IDENTIDAD_H
#define CORE_IDENTIDAD_H

#include <string>

namespace core {

/**
 * IOT-005: identidad del nodo según el firmware que corre.
 *
 * El firmware de producción toma el `device_id` de NVS si es válido (así se
 * aprovisiona cada nodo sin recompilar). El de DEMOSTRACIÓN nunca hereda la
 * identidad de NVS: usa siempre el `device_id` de demo con el que se compiló.
 * Si NVS ya guarda un `device_id` distinto, el firmware demo está corriendo
 * sobre un nodo productivo aprovisionado: la identidad queda BLOQUEADA y el
 * nodo no publica, para que nunca lleguen excursiones simuladas bajo la
 * identidad de un refrigerador real.
 */
struct Identidad {
    std::string deviceId;
    bool bloqueada = false;
};

Identidad resolverIdentidad(const std::string& deviceIdCompilado, const std::string& deviceIdNvs,
                            bool modoDemo);

}  // namespace core

#endif  // CORE_IDENTIDAD_H
