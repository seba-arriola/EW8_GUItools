#!/usr/bin/env bash
# Tests del validador de mapas base (common/tools/check_map_image.py).
#
# Genera fixtures PNG en un tmpdir usando SOLO python3 stdlib (sin ImageMagick)
# y verifica los exit codes del validador. Se engancha en `make -C common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECK="$HERE/../tools/check_map_image.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1 (esperado exit $2, obtenido $3)"; fail=$((fail + 1)); }

# expect <descripcion> <exit_esperado> <cmd...>
expect() {
    local desc="$1" want="$2"; shift 2
    "$@" >/dev/null 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then ok "$desc"; else bad "$desc" "$want" "$got"; fi
}

# --- fixtures ---
python3 - "$TMP" <<'PY'
import os, struct, sys, zlib
tmp = sys.argv[1]

def png(path, w, h, ct):
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = (b"\x00" + b"\x00" * (w * ch)) * h
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + \
               struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, ct, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

png(os.path.join(tmp, "ok_10800x5400_rgb.png"), 10800, 5400, 2)
png(os.path.join(tmp, "bad_ratio_100x40.png"), 100, 40, 2)
png(os.path.join(tmp, "alpha_6000x3000_rgba.png"), 6000, 3000, 6)
png(os.path.join(tmp, "small_100x50.png"), 100, 50, 2)
with open(os.path.join(tmp, "corrupt.bin"), "wb") as f:
    f.write(b"NOT AN IMAGE\x00\x01\x02")
PY

expect "2:1 RGB 10800x5400 -> OK"   0 python3 "$CHECK" "$TMP/ok_10800x5400_rgb.png"
expect "no 2:1 -> exit 4"           4 python3 "$CHECK" --min-width 1 "$TMP/bad_ratio_100x40.png"
expect "RGBA -> exit 5"             5 python3 "$CHECK" "$TMP/alpha_6000x3000_rgba.png"
expect "ancho chico -> exit 6"      6 python3 "$CHECK" "$TMP/small_100x50.png"
expect "archivo corrupto -> exit 3" 3 python3 "$CHECK" "$TMP/corrupt.bin"
expect "no existe -> exit 2"        2 python3 "$CHECK" "$TMP/nope.png"

# La imagen real de entrega (si ya esta instalada) debe pasar en modo estricto.
REAL="$HERE/../../../run_working_v8/params/world_map.jpg"
if [ -f "$REAL" ]; then
    expect "imagen real world_map.jpg -> OK" 0 python3 "$CHECK" --min-width 10800 "$REAL"
fi

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
