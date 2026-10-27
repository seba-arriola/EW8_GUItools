#!/bin/sh
# Contrato de configuracion de csntvpQT (headless, sin DISPLAY).
#
# El .d del GTK no tiene `--print-config`, asi que este script es propio (los
# otros ports reutilizan el de su hermano). Se corre dos veces: sobre el .d real
# del sistema y sobre un .d minimo temporal para fijar los defaults.
set -u

BIN="${BIN:-./csntvpQT}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PARAMS="${PARAMS:-$HERE/../../../run_working_v8/params}"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

has() { # has <salida> <linea-exacta> <descripcion>
    if printf '%s\n' "$1" | grep -qx -- "$2"; then ok "$3"; else bad "$3 ($2)"; fi
}

# --- 1. .d real del sistema ------------------------------------------------
if [ ! -f "$PARAMS/csntvpqt.d" ]; then
    bad "falta $PARAMS/csntvpqt.d"
else
    out="$("$BIN" --print-config "$PARAMS/csntvpqt.d" 2>/dev/null)"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "--print-config del .d real (rc=$rc)"
    else
        ok "--print-config del .d real sale 0"
        has "$out" "module=MOD_CSNTVPQT"            "module"
        has "$out" "in_ring=SLINK_RING"             "in_ring"
        has "$out" "out_ring=PICK_RING"             "out_ring"
        has "$out" "heartbeat_int=30"               "heartbeat_int"
        has "$out" "log_file=1"                     "log_file"
        has "$out" "sta_file=stations_to_view.sta"  "sta_file"
        has "$out" "stations_per_screen=12"         "StationsPerScreen"
        has "$out" "time_window_min=6"              "TimeWindow"
        has "$out" "pick_replace_window=15.0"       "PickReplaceWindow"
        has "$out" "color_wave=#000000"             "waveformsColor"
        has "$out" "color_bg=#FFFFFF"               "backgroundColor"
        has "$out" "color_sep=#D9D9D9"              "separatorColor"
        has "$out" "color_p=#FF0000"                "pColor"
        has "$out" "color_s=#000000"                "sColor"
        has "$out" "color_z=#000000"                "zColor"
        has "$out" "color_n=#1F6FE5"                "nColor (paleta del port)"
        has "$out" "color_e=#1B8A3A"                "eColor (paleta del port)"
        if printf '%s\n' "$out" | grep -qE '^module_id=(175|unresolved)$'; then
            ok "module_id resuelto (175) o unresolved sin earthworm.d"
        else
            bad "module_id inesperado"
        fi
    fi
fi

# --- 2. errores de uso -----------------------------------------------------
"$BIN" --print-config >/dev/null 2>&1
[ $? -eq 2 ] && ok "--print-config sin .d sale 2" || bad "exit != 2 sin .d"

"$BIN" --print-config /no/existe.d >/dev/null 2>&1
[ $? -eq 3 ] && ok ".d inexistente sale 3" || bad "exit != 3 con .d inexistente"

# --- 3. .d minimo: defaults del GTK ---------------------------------------
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

printf 'AAA HHZ XX -- 0.005\n' > "$tmp/min.sta"
cat > "$tmp/min.d" <<EOF
MyModuleId   MOD_CSNTVPQT
InRing       SLINK_RING
OutRing      PICK_RING
HeartBeatInt 30
LogFile      1
StaFile      $tmp/min.sta
EOF

out="$("$BIN" --print-config "$tmp/min.d" 2>/dev/null)"
[ $? -eq 0 ] && ok ".d minimo cargado" || bad ".d minimo"
has "$out" "stations_per_screen=12"   "default StationsPerScreen"
has "$out" "time_window_min=6"        "default TimeWindow"
has "$out" "pick_replace_window=15.0" "default PickReplaceWindow"
has "$out" "color_n=#000000"          "sin nColor hereda de waveformsColor"
has "$out" "color_e=#000000"          "sin eColor hereda de waveformsColor"

# Clave desconocida: se ignora (asi el .d del GTK y el del port comparten formato)
cat >> "$tmp/min.d" <<EOF
EstaClaveNoExiste 42
EOF
out="$("$BIN" --print-config "$tmp/min.d" 2>/dev/null)"
[ $? -eq 0 ] && ok "clave desconocida ignorada" || bad "clave desconocida rompe"

# Obligatoria faltante: exit 3
printf 'MyModuleId MOD_CSNTVPQT\nInRing SLINK_RING\n' > "$tmp/miss.d"
err="$("$BIN" --print-config "$tmp/miss.d" 2>&1 >/dev/null)"
rc=$?
[ "$rc" -eq 3 ] && ok "faltan obligatorias -> exit 3" || bad "faltan obligatorias rc=$rc"
case "$err" in
    *OutRing*) ok "el error nombra las claves que faltan" ;;
    *) bad "el error no nombra OutRing" ;;
esac

if [ "$fail" -eq 0 ]; then
    echo "OK: contrato de configuracion de csntvpQT"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
