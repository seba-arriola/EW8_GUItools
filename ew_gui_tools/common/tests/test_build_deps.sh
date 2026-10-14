#!/bin/bash
# Invariante de fuente: todo Makefile de modulo propio (incluido common) debe
# compilar con dependencias de cabeceras (-MMD -MP + -include de los .d).
#
# Por que: sin ellas, cambiar un .h NO recompila los .o que lo incluyen. El
# modulo queda con el layout de structs viejo mientras la libreria compartida SI
# se recompila, y la ABI se rompe EN SILENCIO: paso con csnhypodbp y
# libcsnwphase.a (el i_master salia con la memoria del struct equivocado -los
# int del evento leidos como double- y la inversion de fase W no arrancaba).
# Ver AGENTS.md §5.
set -u

MOD="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$MOD/../.." && pwd)"

fail=0
n=0
for mk in "$ROOT"/ew_gui_tools/*/Makefile; do
    [ -f "$mk" ] || continue
    n=$((n + 1))
    rel="${mk#$ROOT/}"
    if ! grep -qF -- '-MMD' "$mk"; then
        echo "FALLO: $rel no compila con -MMD (dependencias de cabeceras)"
        fail=1
    fi
    if ! grep -qF -- '-MP' "$mk"; then
        echo "FALLO: $rel no compila con -MP (evita romper si se borra un header)"
        fail=1
    fi
    if ! grep -qF -- '-MF' "$mk"; then
        echo "FALLO: $rel no fija -MF: las deps caerian en *.d y chocarian con"
        echo "       las configuraciones .d del modulo (ew_controller.d, test/*.d)"
        fail=1
    fi
    if ! grep -qF -- '.dep' "$mk"; then
        echo "FALLO: $rel no usa la extension .dep para las dependencias"
        fail=1
    fi
    if ! grep -qF -- '-include $(wildcard' "$mk"; then
        echo "FALLO: $rel no incluye los .dep (-include)"
        fail=1
    fi
    if ! grep -qF -- 'clean-deps' "$mk"; then
        echo "FALLO: $rel no borra los .d en clean"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "OK: $n Makefiles con dependencias de cabeceras"
fi
exit $fail
