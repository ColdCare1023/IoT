@echo off
rem Genera los payloads de ejemplo del contrato v1.0 con el codigo real de src/core
rem y los deja en docs/ejemplos (Visual Studio 2022).
setlocal
set "RAIZ=%~dp0.."
set "DESTINO=%RAIZ%\docs\ejemplos"
if not "%~1"=="" set "DESTINO=%~1"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
where cl >nul 2>nul || (echo No se encontro cl.exe de Visual Studio & exit /b 2)
if not exist "%RAIZ%\.pio\gen" mkdir "%RAIZ%\.pio\gen"
if not exist "%DESTINO%" mkdir "%DESTINO%"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"%RAIZ%\src" ^
   "%RAIZ%\src\core\*.cpp" "%RAIZ%\tools\generar_ejemplos.cpp" ^
   /Fo"%RAIZ%\.pio\gen\\" /Fe"%RAIZ%\.pio\gen\generar_ejemplos.exe" || exit /b 1
"%RAIZ%\.pio\gen\generar_ejemplos.exe" "%DESTINO%"
