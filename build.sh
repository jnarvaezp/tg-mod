#!/bin/bash
# build.sh - automatiza autogen.sh, configure y make para tg-timer.
# Elimina el binario anterior para forzar regeneracion limpia.
# Uso: ./build.sh [targets...]
#   ./build.sh              -> build release (tg-timer)
#   ./build.sh tg-timer-dbg -> build debug
#   ./build.sh clean        -> limpia sin construir

set -e

cd "$(dirname "$0")"

BINARIES=(tg-timer tg-timer-dbg tg-timer-prf tg-timer-vlg)

echo "==> Eliminando binarios anteriores..."
rm -f "${BINARIES[@]}" 2>/dev/null || true

if [ "$1" = "clean" ]; then
    echo "==> Limpieza solicitada. Saliendo."
    exit 0
fi

if [ ! -f configure ]; then
    echo "==> configure no existe. Ejecutando autogen.sh..."
    ./autogen.sh
else
    echo "==> configure ya existe. Saltando autogen.sh."
fi

if [ ! -f Makefile ]; then
    echo "==> Makefile no existe. Ejecutando ./configure..."
    ./configure
else
    echo "==> Makefile ya existe. Saltando ./configure."
fi

if [ $# -gt 0 ]; then
    TARGETS="$*"
else
    TARGETS="tg-timer"
fi

echo "==> Ejecutando make $TARGETS..."
make $TARGETS

echo ""
echo "==> Build exitoso."
ls -lh "${BINARIES[@]}" 2>/dev/null | grep -v 'No such'