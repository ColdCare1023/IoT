@echo off
rem ==========================================================================
rem  Pruebas nativas de src/core/ con MSVC (Windows sin MinGW).
rem
rem  Equivale a `pio test -e native`, que en Windows exige gcc/g++ en el PATH.
rem  Requisitos: Visual Studio 2022 (carga de trabajo "Desarrollo de escritorio
rem  con C++") y la libreria Unity. Si no existe .pio\libdeps\native\Unity,
rem  obtenerla una vez con:   pio pkg install -e native
rem
rem  Uso (desde la carpeta 01-firmware-edge):   scripts\tests_nativos_msvc.cmd
rem ==========================================================================
setlocal
set "RAIZ=%~dp0.."
set "UNITY=%RAIZ%\.pio\libdeps\native\Unity\src"
if not "%UNITY_DIR%"=="" set "UNITY=%UNITY_DIR%"
if not exist "%UNITY%\unity.c" (
  echo No se encontro Unity en "%UNITY%". Ejecutar: pio pkg install -e native
  exit /b 2
)
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 2
set "SALIDA=%RAIZ%\.pio\msvc"
if not exist "%SALIDA%" mkdir "%SALIDA%"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%RAIZ%\src" /I"%UNITY%" ^
   "%RAIZ%\src\core\*.cpp" "%RAIZ%\test\test_core\*.cpp" "%UNITY%\unity.c" ^
   /Fo"%SALIDA%\\" /Fe"%SALIDA%\tests_core.exe" || exit /b 1
"%SALIDA%\tests_core.exe"
