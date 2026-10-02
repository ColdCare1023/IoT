# Evidencia de QA y Validación — HU-10: Autenticación de Dispositivo (Firmware Edge)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-10
- **Título**: Autenticación de Dispositivo (device_id + token único + ACL)
- **Capa**: Edge & Broker MQTT

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Verificar conexión con credencial propia** | `[FW]` | El nodo utiliza `Credenciales.cpp` para validar identidad y token; marcador de fábrica descartado; solo conecta con credencial aprovisionada. | **PASS** | `test_una_credencial_real_vale`, `test_nodo_aprovisionado_se_acepta` (Unity C++) |
| **Probar rechazo con token inválido** | `[QA]` | El firmware recibe CONNACK returnCode=5 ("Not authorized") y registra fallo sin enviar datos no autorizados. | **PASS** | `test_hu10_qa_rechazo_con_token_invalido` |
| **Probar denegación en tópico ajeno** | `[QA]` | La topología de tópicos en `config.h` y `TransmisorMQTT.cpp` asegura que el nodo solo transmita bajo `farmacias/<device_id>/...`. Intentos ajenos son bloqueados por ACL del broker. | **PASS** | `test_hu10_qa_denegacion_en_topico_ajeno` |
| **Capturar logs de EMQX como evidencia** | `[QA]` | Trazas de conexión y denegación de tópicos capturadas en suite de pruebas. | **PASS** | Reporte registrado. |
