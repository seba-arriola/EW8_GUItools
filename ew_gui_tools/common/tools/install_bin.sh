#!/bin/sh
# install_bin.sh - instala binarios en el directorio de EarthWorm de forma ATÓMICA.
#
#   install_bin.sh <dst_dir> <bin> [bin...]
#
# Por qué existe (ver AGENTS.md §7). El `cp` directo que hacían los Makefiles
# tenía dos problemas, los dos reales y los dos silenciosos:
#
#   1) Si el binario destino estaba EJECUTÁNDOSE, `cp` fallaba con
#      "Text file busy" y `make` abortaba: el instalado quedaba viejo.
#   2) Si el binario local ya estaba al día, `make` respondía "Nothing to be
#      done" y NO ejecutaba la regla de copia ⇒ el instalado quedaba AUSENTE.
#      Así se quedó este repo sin `earthworm_8.0/bin/csnhypodbp` y `startstop`
#      no pudo arrancarlo (`execvp failed` → CHILD FATAL ERROR → se cayó todo).
#
# La copia a un temporal + `mv` (rename dentro del mismo directorio) es atómica:
# el proceso que esté corriendo sigue con su inodo viejo y el instalado queda
# siempre al día. El `cmp` final es la garantía de que se copió de verdad.
set -u

if [ $# -lt 2 ]; then
    echo "uso: install_bin.sh <dst_dir> <bin> [bin...]" >&2
    exit 2
fi

dst_dir=$1
shift

if [ ! -d "$dst_dir" ]; then
    echo "install_bin.sh: no existe el directorio destino $dst_dir" >&2
    exit 2
fi

rc=0
for bin in "$@"; do
    base=${bin##*/}
    if [ ! -f "$bin" ]; then
        echo "install_bin.sh: FALTA el binario local $bin (¿compilaste el módulo?)" >&2
        rc=2
        continue
    fi

    tmp="$dst_dir/.$base.tmp.$$"
    if ! cp -p "$bin" "$tmp"; then
        echo "install_bin.sh: no se pudo copiar $bin a $tmp" >&2
        rm -f "$tmp"
        rc=1
        continue
    fi
    if ! mv -f "$tmp" "$dst_dir/$base"; then
        echo "install_bin.sh: no se pudo instalar $dst_dir/$base" >&2
        rm -f "$tmp"
        rc=1
        continue
    fi
    if ! cmp -s "$bin" "$dst_dir/$base"; then
        echo "install_bin.sh: $dst_dir/$base quedó distinto del local" >&2
        rc=1
        continue
    fi

    echo "instalado: $dst_dir/$base"
done

exit $rc
