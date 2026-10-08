#!/bin/bash
# =============================================================================
#  run_all.sh - runner de las pruebas de tank_tools
#
#     ./run_all.sh          solo OFFLINE (no arranca ningun servicio)
#     ./run_all.sh --live   OFFLINE + LIVE (arranca y DETIENE EarthWorm)
#
#  Sale 0 si todos los tests pasan.
# =============================================================================
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LIVE=0
for a in "$@"; do
    case "$a" in
        --live) LIVE=1 ;;
        -h|--help) echo "Uso: $0 [--live]"; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $a" >&2; exit 1 ;;
    esac
done
[ "$LIVE" -eq 1 ] && export TANK_TESTS_LIVE=1

if [ "$LIVE" -eq 1 ]; then
    echo "#####################################################################"
    echo "#  MODO LIVE: se detendra el EarthWorm que este en marcha y se      #"
    echo "#  arrancara en modo replay con EW_PARAMS y EW_LOG AISLADOS.        #"
    echo "#  Al terminar cada prueba el stack queda DETENIDO.                 #"
    echo "#####################################################################"
fi

fail=0
for t in test_remux.sh test_fastmode.sh test_chan_align.sh test_deploy_portable.sh test_picks_manual.sh test_merge_picks.sh test_catalog_report.sh test_calibrate_refiners.sh; do
    echo
    echo "############ $t ############"
    if [ ! -f "$DIR/$t" ]; then
        echo "SKIP (no existe $t)"
        continue
    fi
    bash "$DIR/$t" || fail=1
done

echo
if [ "$fail" -eq 0 ]; then
    echo "############ TODOS LOS TESTS OK ############"
else
    echo "############ HAY FALLOS ############"
fi
exit "$fail"
