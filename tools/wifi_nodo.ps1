# Guarda la contraseña Wi-Fi en el nodo por la consola serie y lo reinicia.
# La contraseña se pide oculta y no queda en el historial ni en el código.
# Uso: powershell -ExecutionPolicy Bypass -File tools\wifi_nodo.ps1 [-Puerto COM6]
param([string]$Puerto = "COM6")

$segura = Read-Host "Contraseña del Wi-Fi para el nodo" -AsSecureString
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($segura)
try {
    $clave = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    $serie = New-Object System.IO.Ports.SerialPort $Puerto, 115200
    # Sin DTR/RTS: abrir el puerto no reinicia la placa.
    $serie.DtrEnable = $false
    $serie.RtsEnable = $false
    $serie.Open()
    Start-Sleep -Milliseconds 500
    $serie.Write("nvs set wifi_pass $clave`n")
    Start-Sleep -Seconds 1
    $respuesta = $serie.ReadExisting()
    $serie.Write("reiniciar`n")
    Start-Sleep -Milliseconds 500
    $serie.Close()
    if ($respuesta -match "wifi_pass -> OK") { "Contraseña guardada. El nodo se está reiniciando." }
    else { "No se confirmó el guardado; respuesta del nodo:"; $respuesta }
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    $clave = $null
}
