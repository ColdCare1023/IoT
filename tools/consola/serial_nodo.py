"""Utilidades de consola serie para diagnosticar el nodo sin abrir un monitor.

    python serial_nodo.py leer [segundos=20]          lee sin resetear el nodo
    python serial_nodo.py arranque [segundos=15]      resetea por EN (RTS) y muestra el arranque
    python serial_nodo.py cmd "<comando>" [segundos=10]   envía un comando (p. ej. "estado")
    python serial_nodo.py host <ip>                   cambia solo mqtt_host y reinicia

El puerto se detecta solo (primer puerto USB que no sea Bluetooth); se puede forzar con la
variable de entorno NODO_PUERTO=COM6. Las líneas que parezcan secretos se ocultan.

Lecturas cortas a propósito (≤ 30 s): con una placa defectuosa, un monitor serie largo llegó a
congelar la laptop. Para cambiar Wi-Fi y contraseña usar red_nodo.py (pide la clave con getpass).
"""
import os
import re
import sys
import time

import serial
from serial.tools import list_ports

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
OCULTAR = re.compile(r"(?i)pass|token|clave|secret|psk|BEGIN")


def puerto() -> str:
    if os.environ.get("NODO_PUERTO"):
        return os.environ["NODO_PUERTO"]
    for p in list_ports.comports():
        if "BTHENUM" not in (p.hwid or "") and p.vid is not None:
            return p.device
    raise SystemExit("No encuentro el ESP32 por USB (define NODO_PUERTO).")


def abrir() -> serial.Serial:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = puerto(), 115200, 0.3
    s.dtr = False  # sin DTR/RTS el nodo no se resetea ni entra al bootloader
    s.rts = False
    s.open()
    return s


def volcar(s: serial.Serial, segundos: float, sin_cap: bool = False) -> None:
    fin, buf = time.time() + segundos, b""
    while time.time() < fin:
        buf += s.read(512)
    for linea in buf.decode("utf-8", "replace").splitlines():
        linea = linea.strip()
        if not linea or (sin_cap and linea.startswith("[CAP]")):
            continue
        print("[oculto]" if OCULTAR.search(linea) else linea)


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    accion = sys.argv[1]
    s = abrir()
    try:
        if accion == "leer":
            s.reset_input_buffer()
            volcar(s, float(sys.argv[2]) if len(sys.argv) > 2 else 20)
        elif accion == "arranque":
            s.rts = True
            time.sleep(0.15)
            s.rts = False
            volcar(s, float(sys.argv[2]) if len(sys.argv) > 2 else 15)
        elif accion == "cmd":
            time.sleep(0.3)
            s.reset_input_buffer()
            s.write((sys.argv[2] + "\n").encode())
            # Si el nodo está bloqueado intentando conectar, tarda en atender la consola.
            volcar(s, float(sys.argv[3]) if len(sys.argv) > 3 else 10, sin_cap=True)
        elif accion == "host":
            time.sleep(0.3)
            s.reset_input_buffer()
            s.write(f"nvs set mqtt_host {sys.argv[2]}\n".encode())
            volcar(s, 10, sin_cap=True)
            s.write(b"reiniciar\n")
            time.sleep(0.5)
            print("Reinicio pedido. El host nuevo debe estar en el certificado del broker.")
        else:
            raise SystemExit(__doc__)
    finally:
        s.close()


if __name__ == "__main__":
    main()
