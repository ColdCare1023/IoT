"""IOT-001: embebe la CA del broker en la imagen del firmware.

Pre-script de PlatformIO (`extra_scripts = pre:scripts/embeber_ca.py`). Lee
`data/certs/root_ca.pem` (público, pero no versionado: depende del broker de
cada despliegue) y genera `include/ca_embebida.h` con el PEM como literal.

- Con CA: el firmware la usa y NO acepta otra desde LittleFS. Con Secure Boot
  la CA queda dentro de la imagen firmada: sustituirla exige firmar una imagen.
- Sin CA: el header declara CA_EMBEBIDA 0 y el firmware recurre a LittleFS
  (desarrollo y CI). El entorno `produccion` define EXIGIR_CA_EMBEBIDA=1 y la
  compilación falla aquí mismo si falta.
"""

import re
from pathlib import Path

Import("env")  # noqa: F821  (lo inyecta PlatformIO)

proyecto = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
origen = proyecto / "data" / "certs" / "root_ca.pem"
destino = proyecto / "include" / "ca_embebida.h"
exigir = any(
    "EXIGIR_CA_EMBEBIDA=1" in str(flag) for flag in env.get("BUILD_FLAGS", [])  # noqa: F821
)

# Uno o varios certificados: la raíz del broker y, si la OTA (IOT-004) se
# descarga de un host con otra cadena, también la de ese host.
PEM = re.compile(r"(?:-----BEGIN CERTIFICATE-----[A-Za-z0-9+/=\s]+-----END CERTIFICATE-----\s*)+")

pem = origen.read_text(encoding="ascii").strip() if origen.exists() else ""
if pem and not PEM.fullmatch(pem.replace("\r", "")):
    raise SystemExit(f"{origen} no contiene solo certificados PEM validos.")
if exigir and not pem:
    raise SystemExit(
        "Entorno de produccion sin CA embebida: coloque el certificado del broker en "
        "data/certs/root_ca.pem (ver data/certs/README.md)."
    )

lineas = "".join(f'    "{linea}\\n"\n' for linea in pem.replace("\r", "").splitlines())
contenido = (
    "// Generado por scripts/embeber_ca.py: no editar ni versionar.\n"
    "#pragma once\n"
    f"#define CA_EMBEBIDA {1 if pem else 0}\n"
    + (f"static const char ROOT_CA_PEM[] =\n{lineas};\n" if pem else "static const char ROOT_CA_PEM[] = \"\";\n")
)
destino.parent.mkdir(exist_ok=True)
if not destino.exists() or destino.read_text(encoding="ascii") != contenido:
    destino.write_text(contenido, encoding="ascii")
print(f"CA del broker {'EMBEBIDA en la imagen' if pem else 'no embebida (se leera de LittleFS)'}.")
