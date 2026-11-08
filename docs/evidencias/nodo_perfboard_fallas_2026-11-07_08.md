# Nodo en perfboard: fallas físicas del 7–8 nov. 2026 y su corrección

Nodo `ESP32_FARMACIA_01`, firmware v1.2.0, intervalo de 10 s. ESP32 DevKitC V4 en headers hembra
sobre perfboard; SHT31 por I2C (SDA = GPIO21 en C14, SCL = GPIO22 en C17), DS18B20 en GPIO4 y MC-38
en GPIO27. El registro completo, con las corridas del paper, está en
`Backend/docs/evidencias/paper_banco/hallazgos.md` (H10).

## Síntomas

| Fecha y hora (Lima) | Qué se vio | Fuente |
|---|---|---|
| 7 nov., desde 15:36 | `sht31_fault: no_response` en todas las lecturas salvo la primera de cada arranque | `thermal_readings.payload` |
| 7 nov., 15:36–18:29 | boot_id de 1063 a 1113; motivos `BROWNOUT` (mayoría), `POWERON`, `TASK_WDT` | eventos `arranque` en `audit_logs` |
| 7 nov. | Al presionar el SHT31 aparecían datos por momentos | operadora |
| 8 nov., arranque | `[SHT31] ERROR: Sin ACK en 0x44: revisar SDA=GPIO21 / SCL=GPIO22 y ADDR.` | consola serie |
| 8 nov., tras resoldar SCL | SHT31 `timeout`; DS18B20 `Timeout de conversion (> 1000 ms)`; puerta siempre abierta | consola serie |

## Causas y correcciones

1. **Laptop a batería (plan "Silent"):** el USB no sostenía los picos de corriente del Wi-Fi y el
   nodo caía por `BROWNOUT`. Con el cargador de la laptop siguió fallando, así que no era la única
   causa.
2. **Soldaduras frías en SDA/SCL del módulo y cable de SCL suelto en C17:** sin reloj I2C el
   sensor no contestaba (`Sin ACK`). Se resoldaron.
3. **VIN/GND del módulo abiertos:** 3,30 V en los cables puente pero 0 V en los pines del SHT31.
   El sensor se alimentaba a medias por las pull-up de I2C: respondía la dirección pero no medía.
   Se resoldaron VIN y GND (8 nov., 12:39).
4. **Bucle de BROWNOUT tras mover la placa (8 nov., 13:38):** unos 195 arranques (boot 1146–1339);
   el nodo reportó `perdidas: total=9`. Se resolvió reconectando el cable.
5. **Laptop suspendida (8 nov., 14:15–17:28):** el USB se apaga y el nodo deja de capturar. Al
   despertar arranca con `BROWNOUT`.

## Verificación

| Ventana (Lima) | Lecturas | SHT31 | DS18B20 | Reinicios | Notas |
|---|---|---|---|---|---|
| 8 nov., 12:40–13:10 (boot 1141) | 180/180, seq 9–188 sin huecos | 180/180 | 180/180 | 0 | SHT31 se enfría del cautín: 29,7 → 26,5 °C; humedad 59 → 70 % |
| 8 nov., 17:28–17:58 (boot 1354) | 180/180, seq 5–184 sin huecos | 180/180 | 180/180 | 0 | 2 desconexiones MQTT sin pérdida, latencia máx. 15,2 s |

## Lecciones para el banco

- Alimentar el nodo con el cargador de pared de 5 V, no con el USB de la laptop.
- Fijar los cables del SHT31 a la placa (cinta o silicona) para que no tiren de las soldaduras.
- Medir voltaje **en el pin del módulo**, no en el cable: el cable puede tener tensión y el pin no.
- `Sin ACK` = el sensor no ve el bus (SCL/SDA/alimentación); `timeout` con ACK = el sensor contesta
  pero no completa la medición (alimentación parcial).
- Con el broker inalcanzable el nodo puede salir por `TASK_WDT` mientras intenta conectar (H1).

## Herramientas

`tools/consola/serial_nodo.py` (leer el serial, ver el arranque, enviar comandos, cambiar
`mqtt_host`) y `tools/consola/red_nodo.py` (cambiar Wi-Fi y broker, con la contraseña por
getpass). Ambas hacen lecturas cortas y ocultan las líneas que parecen secretos.

## Mejoras de firmware que sugiere este episodio (no aplicadas)

- Reintentar `begin()` del SHT31 y liberar el bus I2C (9 pulsos de SCL) tras fallas seguidas: hoy
  el sensor no se reinicializa hasta el siguiente arranque.
- Reintentar el CRC de la DS18B20 hasta 3 veces en el mismo ciclo (SEN-02).

Se dejan para después de las corridas del paper, para no mezclar versiones de firmware.
