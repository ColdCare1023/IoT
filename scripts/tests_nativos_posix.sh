#!/usr/bin/env bash
# ==============================================================================
# Pruebas nativas de src/core/ en macOS / Linux con clang++ / g++
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "${SCRIPT_DIR}/.." && pwd)"
UNITY="${RAIZ}/.pio/libdeps/native/Unity/src"

if [[ -n "${UNITY_DIR:-}" ]]; then
  UNITY="${UNITY_DIR}"
fi

if [[ ! -f "${UNITY}/unity.c" ]]; then
  echo "Error: No se encontró Unity en ${UNITY}" >&2
  exit 2
fi

CXX="${CXX:-clang++}"
SALIDA="/tmp/coldcare_tests_core"

echo "Compilando pruebas nativas con ${CXX}..."
"${CXX}" -std=c++17 -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS \
  -I"${RAIZ}/src" -I"${UNITY}" \
  "${RAIZ}/src/core/"*.cpp \
  "${RAIZ}/test/test_core/"*.cpp \
  "${UNITY}/unity.c" \
  -o "${SALIDA}"

echo "Ejecutando suite de pruebas nativas..."
"${SALIDA}"
