#!/bin/bash
# Pruebas de la combinacion P+S para el modo offline de csnloc:
#   - merge_picks.py (--selftest: combina, ordena, idempotente, fases, dry-run, force)
#   - capture_picks_ps.sh --dry-run (barrido P+S sin escribir)
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
fail=0

echo "=== selftest merge_picks ==="
python3 "$ROOT/tank_tools/merge_picks.py" --selftest || fail=1

echo
echo "=== capture_picks_ps.sh --dry-run (smoke) ==="
TANK=""
for c in "$ROOT/replay/tanks/test120.tank" "$ROOT/replay/tanks/master.tank"; do
    [ -f "$c" ] && { TANK="$c"; break; }
done
if [ -z "$TANK" ]; then
    echo "SKIP (no hay tank en replay/tanks/)"
else
    slug="$(basename "$TANK" .tank)"
    out="$(mktemp -d "${TMPDIR:-/tmp}/test_picks_ps.XXXXXX")"
    if "$ROOT/tank_tools/capture_picks_ps.sh" --dry-run --only "$slug" \
            --pdir "$out/p" --sdir "$out/s" --out "$out/ps" "$TANK"; then
        echo "ok  : dry-run P+S"
    else
        echo "FAIL: dry-run P+S"
        fail=1
    fi
    if [ -e "$out/ps" ]; then
        echo "FAIL: dry-run escribio $out/ps"
        fail=1
    else
        echo "ok  : dry-run no escribio la carpeta combinada"
    fi
    rm -rf "$out"
fi

exit "$fail"
