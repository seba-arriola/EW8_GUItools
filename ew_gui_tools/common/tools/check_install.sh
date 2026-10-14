#!/bin/sh
# check_install.sh - comprueba que TODOS los binarios de los módulos propios
# están instalados en el bin de EarthWorm y al día respecto al binario local.
#
#   check_install.sh          silencioso si todo está bien
#   check_install.sh -v       lista también los OK y los SKIP
#
# exit 0 = todo instalado y al día (los módulos sin compilar se saltan)
# exit 1 = hay binarios FALTANTES o DESACTUALIZADOS
# exit 2 = no existe el directorio de instalación
#
# Por qué existe: el `cp` de los Makefiles podía no ejecutarse nunca (cuando el
# binario local ya estaba al día, `make` decía "Nothing to be done"), y el
# instalado quedaba ausente EN SILENCIO hasta que `startstop` no podía arrancar
# el módulo. Ver AGENTS.md §7 y common/tools/install_bin.sh.
#
# El repo y el destino se pueden sobreescribir (lo usan los tests):
#   EWGUI_REPO_ROOT=<raíz del repo>  EWGUI_INSTALL_DST=<dir del bin>
set -u

root=${EWGUI_REPO_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}
dst=${EWGUI_INSTALL_DST:-$root/earthworm_8.0/bin}
verbose=0
[ "${1:-}" = "-v" ] && verbose=1

if [ ! -d "$dst" ]; then
    echo "check_install: no existe el directorio de instalación $dst" >&2
    exit 2
fi

# Inventario módulo:binarios (el mismo que arranca startstop_unix.d).
entries="csnmags:csnmags
csnhypodbp:csnhypodbp
csnloc:csnloc
csnrv:csnrv
csnstaevdisp:csnstaevdisp
csntvp:csntvp
csnwphase:csnwphase
ew_controller:ew_controller
hyp2000_ring:hyp2000_ring
hypodb:hypodb_ingest
nlloc_ring:nlloc_ring
pickS:pickS
wavestore:wstore_ingest,tank2store,store2tank,wstore_query"

bad=0
for entry in $entries; do
    mod=${entry%%:*}
    bins=$(printf '%s' "${entry#*:}" | tr ',' ' ')
    for b in $bins; do
        src="$root/ew_gui_tools/$mod/$b"
        ins="$dst/$b"
        if [ ! -f "$src" ]; then
            if [ -f "$ins" ]; then
                echo "AVISO $ins está instalado pero el módulo $mod no está compilado  ->  make -C ew_gui_tools/$mod"
            else
                [ "$verbose" = 1 ] && echo "SKIP  $b (módulo $mod sin compilar)"
            fi
            continue
        fi
        if [ ! -f "$ins" ]; then
            echo "FALTA $ins  (compilado: $src)  ->  make -C ew_gui_tools/$mod"
            bad=1
        elif ! cmp -s "$src" "$ins"; then
            echo "DESACTUALIZADO $ins  (distinto de $src)  ->  make -C ew_gui_tools/$mod"
            bad=1
        else
            [ "$verbose" = 1 ] && echo "OK    $b"
        fi
    done
done

exit $bad
