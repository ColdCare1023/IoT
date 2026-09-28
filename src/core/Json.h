#ifndef CORE_JSON_H
#define CORE_JSON_H

#include <cstdint>
#include <string>

namespace core {

/**
 * JSON mínimo, sin dependencias, para el contrato del nodo.
 *
 * Escritura: `ObjetoJSON` arma un objeto plano campo a campo. Garantiza que
 * NaN/infinito nunca salgan como número (se emiten como `null`) y escapa las
 * cadenas. Se prefiere a ArduinoJson porque el payload es plano y de tipos
 * fijos: no hace falta un documento en el heap por cada lectura y así la
 * serialización compila y se prueba en el host.
 *
 * Lectura: `buscarCampo` recorre SOLO el primer nivel de un objeto y tolera
 * espacios alrededor de `:` y `,`. Es importante: `json.dumps()` de Python
 * emite `"clave": valor` (con espacio) y un buscador literal de `"clave":`
 * no encontraría nada — el acuse lógico del backend nunca se reconocería.
 */

std::string escaparJSON(const std::string& entrada);

class ObjetoJSON {
public:
    ObjetoJSON();

    void cadena(const char* clave, const std::string& valor);
    void entero(const char* clave, int64_t valor);
    void sinSigno(const char* clave, uint64_t valor);
    /// Número con `decimales` decimales; `null` si no es finito.
    void real(const char* clave, double valor, int decimales = 2);
    void booleano(const char* clave, bool valor);
    void nulo(const char* clave);
    /// Inserta un fragmento JSON ya serializado (p. ej. otro objeto).
    void crudo(const char* clave, const std::string& fragmento);

    /// Cierra el objeto y devuelve el texto. Llamadas posteriores devuelven
    /// el mismo texto.
    const std::string& cerrar();

private:
    void _clave(const char* clave);
    std::string _texto;
    bool _vacio = true;
    bool _cerrado = false;
};

/// Tipo del valor encontrado por `buscarCampo`.
enum class TipoValorJSON { NoEncontrado, Cadena, Numero, Booleano, Nulo, Compuesto };

/// Busca `campo` en el primer nivel del objeto `json`. Para cadenas devuelve
/// el contenido ya des-escapado (solo escapes simples; `\uXXXX` se conserva
/// literal). Para números/booleanos/null devuelve el texto crudo.
TipoValorJSON buscarCampo(const std::string& json, const std::string& campo, std::string& valor);

/// Atajos tipados. Devuelven false si el campo no existe o tiene otro tipo.
bool leerCadena(const std::string& json, const std::string& campo, std::string& salida);
bool leerSinSigno32(const std::string& json, const std::string& campo, uint32_t& salida);

}  // namespace core

#endif  // CORE_JSON_H
