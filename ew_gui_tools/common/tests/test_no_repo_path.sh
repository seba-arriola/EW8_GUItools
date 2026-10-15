#!/usr/bin/env bash
# Invariante: ningun fichero VERSIONADO contiene la ruta absoluta de ESTE repo.
#
# Por que: una ruta absoluta escrita a mano (una receta en la doc, una fixture)
# ata el repo a una carpeta concreta; al moverlo (p. ej. para dejar los worktrees
# como hermanos dentro de `Dev/ew8/`) o al clonarlo en otra ruta, esa receta
# apunta a algo que ya no existe y nadie se entera. Este test lo detecta.
#
# Lo que SI puede aparecer: rutas absolutas a recursos de FUERA del repo
# (`/mnt/d/resources/...`, `/home/<usuario>/Dev/wphase/...`), porque mover el
# repo no las cambia.
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"

pass=0; fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

hits="$(git -C "$ROOT" grep -n -F -e "$ROOT" 2>/dev/null || true)"

if [ -z "$hits" ]; then
    ok "ningun fichero versionado cita la ruta absoluta del repo"
else
    bad "hay ficheros versionados con la ruta absoluta del repo ($ROOT)"
    printf '%s\n' "$hits" | sed 's/^/       /'
    echo "       (usa una ruta relativa o un marcador como <ruta-del-repo>)"
fi

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
