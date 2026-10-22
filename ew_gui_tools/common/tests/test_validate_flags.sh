#!/usr/bin/env bash
# Invariante de `ew8_compile.sh --validate`:
#   - los flags de validacion de configuracion NO viven en el script: cada
#     Makefile declara `VALIDATE_FLAGS` y expone `print-VALIDATE_FLAGS`.
#   - el script no vuelve a tener una tabla de nombres de modulo.
#   - cada `.d` que se valida existe en run_working_v8/params (si no, el paso
#     falla o se salta en silencio).
#
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail
export LC_ALL=C

root="$(cd "$(dirname "$0")/../../.." && pwd)"
params="$root/run_working_v8/params"
fail=0

err() { echo "FAIL: $*"; fail=1; }

# make anidado sin heredar el jobserver del `make check` padre.
flags_of() {  # flags_of <modulo>
    env -u MAKEFLAGS -u MFLAGS \
        make -C "$root/ew_gui_tools/$1" -s --no-print-directory \
        print-VALIDATE_FLAGS 2>/dev/null
}

# 1) Contrato por modulo (lo que consume la fase --validate).
expect() {  # expect <modulo> <linea1> [linea2 ...]
    mod=$1; shift
    want="$(printf '%s\n' "$@")"
    got="$(flags_of "$mod" || true)"
    if [ "$got" = "$want" ]; then
        echo "OK: $mod declara $(printf '%s\n' "$want" | wc -l) comando(s) de validacion"
    else
        err "$mod VALIDATE_FLAGS: esperado [$want] obtenido [$got]"
    fi
}

expect csnhypodbp \
    'csnhypodbp|--print-config csnhypodbp.d' \
    'csnhypodbp|--check-profiles csnhypodbp.d'
expect csnrv \
    'csnrv|--print-config csnrv.d' \
    'csnrv|--check-overlays csnrv.d'
expect csnstaevdisp 'csnstaevdisp|--check-overlays csnstaevdisp.d'
expect csnwphase 'csnwphase|csnwphase.d --print-config'
expect pickml 'pickml|pickml_phasenetwc.d --print-config'

# 2) Un modulo sin validacion no imprime nada (=> SKIP, no FAIL).
for mod in csnloc pickS csnmags; do
    got="$(flags_of "$mod" || true)"
    [ -z "$got" ] || err "$mod no deberia declarar VALIDATE_FLAGS: [$got]"
done

# 3) El script no tiene la tabla de nombres: la deriva del Makefile.
script="$root/ew8_compile.sh"
grep -q 'print-VALIDATE_FLAGS' "$script" || \
    err "ew8_compile.sh no usa print-VALIDATE_FLAGS"
if grep -qE '^\s*csn(hypodbp|rv|staevdisp|wphase|loc)\|' "$script"; then
    err "ew8_compile.sh volvio a tener una tabla de flags con nombres de modulo"
fi

# 4) Cada `.d` referenciado existe (y el binario se llama como el Makefile).
for mod in csnhypodbp csnrv csnstaevdisp csnwphase pickml; do
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        d="${line#*|}"
        for a in $d; do
            case "$a" in
                *.d)
                    [ -f "$params/$a" ] || \
                        err "$mod valida $a, que no existe en run_working_v8/params"
                    ;;
            esac
        done
    done <<EOF
$(flags_of "$mod" || true)
EOF
done

if [ "$fail" -eq 0 ]; then
    echo "OK: VALIDATE_FLAGS derivado de los Makefiles, .d presentes"
fi
exit "$fail"
