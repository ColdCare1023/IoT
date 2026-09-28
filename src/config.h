#ifndef CONFIG_H
#define CONFIG_H

/**
 * ThermoTrace Edge — configuración del nodo ESP32 DevKitC V4.
 *
 * Todo lo que un técnico puede necesitar cambiar por nodo se sobreescribe con
 * `build_flags` en platformio.ini (o, en tiempo de ejecución, por NVS desde la
 * consola serie). NINGUNA credencial vive en este archivo (RNF-05).
 */

// =========================================================================
// Identidad y versión
// =========================================================================
#ifndef DEVICE_ID
  #define DEVICE_ID "ESP32_FARMACIA_01"   // único por nodo; NVS "device_id" lo reemplaza
#endif
#define FW_MAYOR  1
#define FW_MENOR  2
#define FW_PARCHE 0

// =========================================================================
// Credenciales — vacías a propósito (RNF-05). Precedencia: NVS > build_flags.
// =========================================================================
#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif
#ifndef MQTT_HOST
  #define MQTT_HOST ""                    // p. ej. xxxxxxxx.ala.us-east-1.emqxsl.com
#endif
#ifndef MQTT_TOKEN
  #define MQTT_TOKEN ""
#endif
#define NVS_NAMESPACE "thermotrace"

// =========================================================================
// Pines (ver 02-hardware-circuito/OE2.I2_circuito_y_BOM.md)
// =========================================================================
#define PIN_DS18B20   4    // 1-Wire, pull-up EXTERNO 4,7 kΩ a 3V3, alimentación de 3 hilos
#define PIN_I2C_SDA   21   // SHT31
#define PIN_I2C_SCL   22   // SHT31
#define SHT31_DIRECCION 0x44
#define PIN_MC38      15   // reed switch a GND, pull-up INTERNO (strapping: ver OE2.I2)
#define PIN_BOTON_DEMO 0   // botón BOOT de la placa; solo se usa en el entorno `demo`
#define PIN_SENSADO_ENERGIA 27  // opcional: divisor desde el adaptador de 5 V ANTES del Mini UPS

// ¿Hay MC-38 instalado en este nodo? Sin él, door_open = null (HU-04 esc. 4).
#ifndef MC38_INSTALADO
  #define MC38_INSTALADO 1
#endif

// HU-52: por defecto NO hay sensado de energía. Las lecturas ADC de batería en
// GPIO 34 se eliminaron: con el Mini UPS Forza no existe una señal de nivel
// que medir y la entrada flotante producía valores falsos. La continuidad se
// demuestra por la secuencia ininterrumpida boot_id/seq_no durante el corte.
// Con =1 se usa una entrada DIGITAL (GPIO 27) que ve el 5 V del adaptador de
// pared antes del Mini UPS → power_source = "mains"/"backup" en cada lectura.
#ifndef SENSADO_ENERGIA_INSTALADO
  #define SENSADO_ENERGIA_INSTALADO 0
#endif
// Autonomía del Mini UPS MEDIDA en el ensayo C-6 (minutos). 0 = aún no medida:
// no se emite el aviso estimado de autonomía crítica (HU-52 esc. 3).
#ifndef AUTONOMIA_RESPALDO_MEDIDA_MIN
  #define AUTONOMIA_RESPALDO_MEDIDA_MIN 0
#endif
#define AVISO_AUTONOMIA_PCT 80

// Modo demo (entorno `demo`): permite inyectar excursiones SIMULADAS marcadas
// con "simulated": true. Jamás compilar en el firmware del local real.
#ifndef MODO_DEMO
  #define MODO_DEMO 0
#endif

// =========================================================================
// Muestreo
// =========================================================================
// Valor provisional: el definitivo se fija tras el piloto (HU-01 esc. 1).
// Se puede cambiar por NVS ("intervalo_s", 10..3600) sin recompilar.
#define INTERVALO_MUESTREO_S_DEFECTO 30
#define INTERVALO_MUESTREO_S_MIN     10
#define INTERVALO_MUESTREO_S_MAX     3600

// 750 ms es la conversión máxima a 12 bits según hoja de datos.
#define DS18B20_TIMEOUT_CONVERSION_MS 1000
#define SHT31_REINTENTOS              3
#define SHT31_TIMEOUT_I2C_MS          50
#define MC38_POLL_MS                  50
#define MC38_DEBOUNCE_MS              50
#define UMBRAL_PUERTA_PROLONGADA_S    60   // HU-04 esc. 3 (configurable)

// =========================================================================
// MQTT / EMQX Cloud Serverless — solo TLS en 8883, sin fallback a 1883
// =========================================================================
// HU-14: el puerto NO es configurable. Definirlo por build flag es un error
// de compilación, no una advertencia: no existe forma de apuntar a 1883.
#if defined(MQTT_PORT)
  #error "MQTT_PORT no es configurable: solo se admite mqtts (TLS) en 8883 (HU-14)."
#endif
#define MQTT_PORT                 8883
#define MQTT_QOS                  1
#define MQTT_KEEPALIVE_S          60
#define MQTT_COMMAND_TIMEOUT_MS   5000   // espera de CONNACK / PUBACK / SUBACK
#define TLS_HANDSHAKE_TIMEOUT_MS  10000
#define ACK_LOGICO_TIMEOUT_MS     8000   // espera del acuse del backend tras el PUBACK
#define CERT_CA_ARCHIVO           "/certs/root_ca.pem"
#define MQTT_BUFFER_BYTES         1024

// Tópicos: farmacias/<device_id>/{lecturas|eventos|ack}
#define TOPICO_PREFIJO "farmacias/"

// =========================================================================
// Buffer offline (LittleFS, partición "littlefs" de 1,6 MB)
// =========================================================================
#define LITTLEFS_ETIQUETA_PARTICION "littlefs"
#ifndef BUFFER_REGISTROS_POR_SEGMENTO
  #define BUFFER_REGISTROS_POR_SEGMENTO 100   // 4000 B ≈ 1 bloque
#endif
#ifndef BUFFER_MAX_SEGMENTOS
  #define BUFFER_MAX_SEGMENTOS 300            // 30 000 lecturas ≈ 10,4 días a 30 s (reducir solo en pruebas)
#endif
#define BUFFER_UMBRAL_AVISO_PCT       80
#define BUFFER_UMBRAL_REARME_PCT      70
#define BUFFER_MAX_CUARENTENA         250
#define BUFFER_RESERVA_BYTES          (16 * 1024)
#define DRENAJE_MAX_POR_CICLO         20

// Pausa entre intentos de drenaje cuando el backend no acusa (backoff propio,
// para no republicar en ráfaga si FastAPI o PostgreSQL están caídos).
#define DRENAJE_REINTENTO_BASE_MS     5000
#define DRENAJE_REINTENTO_MAX_MS      60000

// =========================================================================
// Wi-Fi — backoff exponencial (HU-08)
// =========================================================================
#define WIFI_BACKOFF_BASE_MS      1000
#define WIFI_BACKOFF_MAX_MS       60000
#define WIFI_BACKOFF_FACTOR       2
#define WIFI_TIMEOUT_ASOCIACION_MS 15000
#define WIFI_UMBRAL_DIAGNOSTICO_INTENTOS 5

// =========================================================================
// Tiempo y diagnóstico
// =========================================================================
#define NTP_SERVIDOR_1 "pool.ntp.org"
#define NTP_SERVIDOR_2 "time.google.com"
#define NTP_SERVIDOR_3 "time.cloudflare.com"
#define NTP_TIMEOUT_MS 10000
#define INTERVALO_ESTADO_MS   300000   // evento "estado_nodo" cada 5 min (HU-48, C-5)
#define CAPACIDAD_EVENTOS_RAM 24

// =========================================================================
// Registro por serie. `LOG_I` se puede silenciar; la línea [CAP] de cada
// captura se imprime SIEMPRE: la usan los scripts de QA (ensayo doméstico).
// =========================================================================
#ifndef LOG_NIVEL_INFO
  #define LOG_NIVEL_INFO 1
#endif
#if LOG_NIVEL_INFO
  #define LOG_I(tag, fmt, ...) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
  #define LOG_I(tag, fmt, ...) ((void)0)
#endif
#define LOG_E(tag, fmt, ...) Serial.printf("[%s] ERROR: " fmt "\n", tag, ##__VA_ARGS__)

#endif  // CONFIG_H
