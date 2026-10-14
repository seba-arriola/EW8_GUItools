#!/bin/sh
# check_install.sh - comprueba que TODOS los binarios de los módulos propios
# están instalados en el bin de EarthWorm y al día respecto al binario local.
#
#   check_install.sh          silencioso si todo está bien
#   check_install.sh -v       lista también los OK y los SKIP
#
# exit 0 = todo instalado y al día (los módulos sin compilar se saltan)
# exit 1 = hay binarios FALTANTES o DESACTUALIZADOS
# exit 2 = no existe el directorio de instalación, o no hay ninguna receta de
#          instalación que mirar (no se puede comprobar nada)
#
# El inventario NO está escrito aquí: se DERIVA de la receta de instalación de
# cada Makefile de módulo (la misma que usa `make install`,
# `sh ../common/tools/install_bin.sh <dst> <bin...>`), resolviendo
# $(TARGET)/$(MODULE) con las variables del propio Makefile. Así, añadir un
# módulo, renombrar un binario o cambiarle el directorio no obliga a tocar este
# script. Antes era una lista literal "módulo:binarios" que se quedaba vieja (y
# que AGENTS.md documentaba como espejo de startstop_unix.d).
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

# Binarios que instala un módulo: se leen de SU Makefile (la receta de
# `make install`), resolviendo $(TARGET)/$(MODULE) con sus propias variables.
mk_bins() {
    mkf=$1
    line=$(grep -E '^[[:space:]]*sh[[:space:]].*install_bin\.sh' "$mkf" 2>/dev/null | head -n1)
    [ -n "$line" ] || return 0
    bins=$(printf '%s\n' "$line" | sed -n 's/.*install_bin\.sh[[:space:]]*[^[:space:]]*[[:space:]]*//p')
    tgt=$(sed -n 's/^[[:space:]]*TARGET[[:space:]]*=[[:space:]]*//p' "$mkf" | head -n1)
    mvar=$(sed -n 's/^[[:space:]]*MODULE[[:space:]]*=[[:space:]]*//p' "$mkf" | head -n1)
    for b in $bins; do
        case "$b" in
            '$(TARGET)') b=$tgt ;;
            '$(MODULE)') b=$mvar ;;
        esac
        [ -n "$b" ] && printf '%s\n' "$b"
    done
}

bad=0
nbin=0
for mkf in "$root"/ew_gui_tools/*/Makefile; do
    [ -f "$mkf" ] || continue
    mod=$(basename "$(dirname "$mkf")")
    for b in $(mk_bins "$mkf"); do
        nbin=$((nbin + 1))
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

if [ "$nbin" -eq 0 ]; then
    echo "check_install: ningún Makefile de $root/ew_gui_tools declara una receta de instalación" >&2
    exit 2
fi

exit $bad
