# Evidencia de QA y Validación — HU-06: Persistencia en Buffer Offline (LittleFS)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-06
- **Título**: Persistencia en Buffer Offline (LittleFS)
- **Capa**: Edge (ESP32)
- **Fecha de ejecución**: 02/10/2026
- **Equipo bajo prueba**: ESP32 DevKitC V4 (ESP32-D0WD-V3) en protoboard, firmware v1.2.0 (`esp32dev`), DS18B20 en GPIO 4, SHT31 en GPIO 21/22 (0x44), MC-38 en GPIO 27.
- **Entorno**: nodo autónomo por USB, monitor serie a 115200 baudios. Para el 80 % y la saturación se usó un firmware de prueba con capacidad reducida (`-DBUFFER_REGISTROS_POR_SEGMENTO=10 -DBUFFER_MAX_SEGMENTOS=3` = 30 registros, intervalo 10 s).

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Probar guardado sin conectividad** | `[FW]` | Sin Wi-Fi cada lectura queda en LittleFS (`g=OK`) y los pendientes crecen. | **PASS** | Sección 3 |
| **Probar recuperación tras reinicio** | `[FW]` | Tras reiniciar, el buffer se remonta con los pendientes intactos. | **PASS (reinicio por software y recarga de firmware)** | `Montado: N pendientes` (sección 4) |
| **Probar aviso al 80 % de capacidad** | `[FW]` | Con ocupación ≥ 80 % se emite el aviso y el evento `buffer_80`. | **PASS (aviso observado al arrancar con el buffer sobre el umbral; el cruce en vivo está probado en host: `test_aviso_80_por_ciento_una_vez_y_se_rearma`)** | Sección 5 |
| **Probar registro de saturación** | `[FW]` | Al 100 % se preservan las lecturas antiguas, la nueva se marca `SATURADO` y el contador persistente crece con su periodo. | **PASS** | Sección 5 |
| **Medir capacidad y duración offline** | `[QA]` | Medición con la capacidad real (30 000 registros). | **PENDIENTE** | Solo cálculo teórico (~10 días a 30 s) |

---

## 3. Captura sin red: los pendientes crecen
```text
12:26:10.595 > [CAP] boot=12 seq=1 t=- tq=nosync ds=24.56 ds_f=- sht=24.96 hr=70.97 sht_f=- door=0 door_s=0 g=OK q=69 ocup=0
12:26:40.349 > [CAP] boot=12 seq=2 t=- tq=nosync ds=24.50 ds_f=- sht=25.02 hr=70.93 sht_f=- door=0 door_s=0 g=OK q=70 ocup=0
12:27:10.266 > [CAP] boot=12 seq=3 t=- tq=nosync ds=24.63 ds_f=- sht=24.99 hr=70.95 sht_f=- door=0 door_s=0 g=OK q=71 ocup=0
12:27:40.434 > [CAP] boot=12 seq=4 t=- tq=nosync ds=24.63 ds_f=- sht=25.00 hr=70.96 sht_f=- door=0 door_s=0 g=OK q=72 ocup=0
```

## 4. Recuperación tras reinicio
```text
11:52:57.940 > [BOOT] boot_id=5 reset=SOFTWARE fw=v1.2.0 device=ESP32_FARMACIA_01 intervalo_s=10 mc38=1 sensado_energia=0 demo=0 credenciales=build_flags
11:52:58.142 > [Buffer] Montado: 9 pendientes en 1 segmentos, 30% ocupado, 1576 KB libres.
12:01:53.472 > [Buffer] Montado: 30 pendientes en 3 segmentos, 100% ocupado, 1576 KB libres.
12:05:36.040 > [Buffer] Montado: 30 pendientes en 3 segmentos, 100% ocupado, 1576 KB libres.
```

## 5. 80 % y saturación (capacidad reducida a 30)
```text
11:55:18.263 > [CAP] boot=5 seq=15 t=- tq=nosync ds=24.63 ds_f=- sht=24.87 hr=72.03 sht_f=- door=0 door_s=0 g=OK q=24 ocup=80
11:56:18.364 > [CAP] boot=5 seq=21 t=- tq=nosync ds=24.56 ds_f=- sht=24.87 hr=72.06 sht_f=- door=0 door_s=0 g=OK q=30 ocup=100
11:56:28.289 > [Captura] ERROR: Lectura seq=22 NO almacenada (SATURADO): contada como perdida.
11:56:28.289 > [CAP] boot=5 seq=22 t=- tq=nosync ds=24.56 ds_f=- sht=24.86 hr=72.04 sht_f=- door=0 door_s=0 g=SATURADO q=30 ocup=100
...
11:59:13.140 > reloj=unsynced | buffer: pendientes=30/30 ocupacion=100% libres=1576KB cuarentena=0
11:59:13.140 > perdidas: total=17 episodio=17 abierto=1 | energia=sin_sensado |
... (tras la correccion: avisos emitidos sin red)
12:05:36.244 > [Evento] Encolado 'buffer_80'.
12:05:36.244 > [Buffer] ERROR: Ocupacion 100% (umbral de aviso 80%).
12:05:36.244 > [Evento] Encolado 'buffer_saturado'.
12:05:36.244 > [Buffer] ERROR: Muestras no almacenadas: total=53, episodio=53 (abierto), seq 22 a 21.
```

## 6. Pendiente / fuera de alcance de esta evidencia
- Corte de energía real (desenchufar) a mitad de escritura: pendiente; lo cubren hoy las pruebas de host `test_corte_a_mitad_de_escritura`.
- Defectos encontrados en esta prueba y corregidos (commit `fix(HU-06)`): el aviso no salía sin red y se encolaba un evento por cada muestra perdida.
