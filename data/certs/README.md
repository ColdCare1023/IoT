# Certificado CA del broker (HU-09)

Colocar aquí `root_ca.pem`: el certificado raíz de la CA que firma el endpoint
TLS de **EMQX Cloud Serverless**. Se descarga desde la consola de EMQX
(Deployment → Overview → *CA Certificate*). Es público: no es un secreto.

```bash
pio run -e esp32dev -t uploadfs
```

> ⚠️ `uploadfs` **reescribe toda la partición LittleFS**, incluido el buffer
> offline. Hacerlo solo al aprovisionar el nodo, **antes** de instalarlo en el
> local, o después de verificar en el dashboard que `pendientes = 0`.

Sin este archivo el nodo **no conecta** (no existe fallback a texto plano ni
`setInsecure()`): sigue capturando y guardando en LittleFS, y lo indica en el
monitor serie.
