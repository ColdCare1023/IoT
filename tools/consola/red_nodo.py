"""Cambia la red Wi-Fi y el broker del nodo por la consola serie.

    python red_nodo.py "<SSID 2,4 GHz>" <ip_del_broker>

La contraseña se escribe con getpass: no se ve, no se guarda y no se imprime (la respuesta del
nodo también la oculta). El ESP32 solo se conecta a redes de 2,4 GHz. La IP del broker tiene que
estar en el certificado del servidor (SAN), o el TLS se rechaza.
"""
import getpass
import os
import sys
import time

import serial
from serial.tools import list_ports

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if len(sys.argv) < 3:
    raise SystemExit(__doc__)
SSID, HOST = sys.argv[1], sys.argv[2]

puerto = os.environ.get("NODO_PUERTO") or next(
    (p.device for p in list_ports.comports() if "BTHENUM" not in (p.hwid or "") and p.vid is not None), None)
if not puerto:
    raise SystemExit("No encuentro el ESP32 por USB (define NODO_PUERTO).")
clave = getpass.getpass(f"Contraseña de '{SSID}' (no se ve al escribir) y Enter: ")
if not clave:
    raise SystemExit("Contraseña vacía: no cambié nada.")

s = serial.Serial(None, 115200, timeout=0.3)
s.port, s.dtr, s.rts = puerto, False, False
s.open()
time.sleep(0.5)
s.reset_input_buffer()
for c in (f"nvs set wifi_ssid {SSID}", f"nvs set wifi_pass {clave}", f"nvs set mqtt_host {HOST}"):
    s.write((c + "\n").encode())
    time.sleep(1.5)
fin, vistas = time.time() + 2, set()
while time.time() < fin:
    linea = s.readline().decode("utf-8", "replace").strip()
    if linea.startswith("nvs set") and linea not in vistas:
        vistas.add(linea)
        print(linea.replace(clave, "***"))
s.write(b"reiniciar\n")
time.sleep(0.5)
s.close()
del clave
if len(vistas) < 3:
    print("AVISO: no vi las 3 confirmaciones; si el nodo estaba ocupado conectando, repite.")
print("Listo: el nodo se reinicia con la red nueva.")
