#!/usr/bin/env bash
# Falla si alguna capa pura (core/dsp/wave/geo) incluye GTK.
#
# Regla de frontera (docs/GTK4-MIGRATION.md §3.2): todo GTK vive en archivos
# cuyo nombre contiene "gtk" (p. ej. view_gtk3.c) o en la capa de acciones.
# Los headers de UI permitidos (view.h, actions.h) también quedan exentos.
set -euo pipefail
cd "$(dirname "$0")/.."

fail=0

check_file() {
    local f="$1"
    case "$f" in
        *gtk*) return 0 ;;                       # view_gtk3.c / view_gtk4.c
        */actions.h|*/view.h) return 0 ;;        # headers de UI
    esac
    if grep -nE '#[[:space:]]*include[[:space:]]*<gtk/gtk\.h>' "$f" >/dev/null; then
        echo "ERROR: $f incluye gtk/gtk.h y no es una capa de vista/UI"
        fail=1
    fi
}

for f in src/*.c include/ewgui/*.h; do
    [ -e "$f" ] || continue
    check_file "$f"
done

if [ "$fail" -ne 0 ]; then
    echo "check_no_gtk: FALLO"
    exit 1
fi
echo "check_no_gtk: OK"
