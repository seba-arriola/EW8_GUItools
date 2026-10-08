#!/bin/bash
# Pruebas de las herramientas de validación de pickS vs picadas manuales:
#   - picks_s_manual.py (parseo .dat, match por estación, residual, cobertura)
#   - gen_guide_p.py    (guía P para el modo hybrid)
#   - ttp (predicción IASP91 P/S, si está compilado)
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
fail=0

echo "=== selftest picks_s_manual ==="
python3 "$ROOT/tank_tools/picks_s_manual.py" selftest || fail=1

echo "=== selftest gen_guide_p ==="
python3 "$ROOT/tank_tools/gen_guide_p.py" --selftest || fail=1

echo "=== test_ttp (csnloc) ==="
if [ -x "$ROOT/ew_gui_tools/csnloc/ttpred" ]; then
    sh "$ROOT/ew_gui_tools/csnloc/test/test_ttp.sh" || fail=1
else
    echo "SKIP (ttpred no compilado)"
fi

exit "$fail"
