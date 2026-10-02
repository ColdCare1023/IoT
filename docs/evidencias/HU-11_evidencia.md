# Evidencia de QA y Validación — HU-11: Publicación de Telemetría (QoS 1) con Idempotencia (Firmware Edge)

## 1. Resumen de la Historia de Usuario
- **ID**: HU-11
- **Título**: Publicación de Telemetría (QoS 1) con Idempotencia
- **Capa**: Edge & Backend Ingestion

---

## 2. Checklist de Verificación y Resultados

| Tarea | Rol | Criterio Verificado | Estado | Evidencia |
|---|---|---|---|---|
| **Verificar espera de PUBACK** | `[FW]` | En transmisión QoS 1, el nodo espera el PUBACK del broker. El PUBACK no elimina la lectura del buffer (regla de oro: solo el acuse lógico del backend libera el registro). | **PASS** | `TransmisorMQTT.cpp` & `test_sin_acuse_logico_no_se_borra_nada` |
| **Probar reintento con misma identidad** | `[FW]` | Cada retransmisión conserva estrictamente `device_id`, `boot_id` y `seq_no` originales. | **PASS** | `test_fifo_estricto_entre_segmentos` |
| **Probar descarte de lectura duplicada** | `[BE]` | La recepción repetida responde `duplicate` sin duplicar en base de datos. | **PASS** | `backend/tests/test_hu11_idempotencia.py` |
| **Probar registro de conflicto de payload** | `[BE]` | Si llega la misma tupla con datos modificados, se registra en tabla `conflictos`. | **PASS** | `backend/tests/test_hu11_idempotencia.py` |
| **Verificar paso a buffer al agotar reintentos** | `[FW]` | Si se agotan los reintentos, el dato se preserva en buffer LittleFS sin pérdidas. | **PASS** | `test_saturacion_preserva_antiguas_y_cuenta_perdidas` (Unity C++) |
