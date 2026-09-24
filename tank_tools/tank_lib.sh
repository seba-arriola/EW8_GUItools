#!/bin/bash
# =============================================================================
#  tank_lib.sh - helpers PUROS compartidos por tank_tools
#
#  A diferencia de tests/lib.sh, este fichero:
#    - NO instala traps
#    - NO lleva contadores PASS/FAIL ni funciones check_*
#    - NO crea directorios temporales implicitos
#  Por eso lo pueden usar tanto la suite de pruebas como los runners.
#
#  Uso:
#     source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/tank_lib.sh"
#
#  Lo que aporta:
#    - rutas del repo y binarios (ew_bin, tanksniff)
#    - medidas de un tank deducidas de tanksniff (span, nmsgs, SCNL, orden)
#    - recortes de ventana (tank_cut_at / tank_cut)
#    - copia aislada de run_working_v8/params (make_params_copy)
#    - helpers de logs y rings
# =============================================================================

# -----------------------------------------------------------------------------
#  Rutas y binarios
# -----------------------------------------------------------------------------
TANK_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$TANK_LIB_DIR/.." && pwd)"
TANKS_REAL="$ROOT_DIR/replay/tanks"
MSEED_REAL="$ROOT_DIR/mseed/test2.mseed"
MSEED_DIR="$ROOT_DIR/mseed"
TANK_REPO="$ROOT_DIR/tank_repo"
TANK_REPLAY="$ROOT_DIR/tank_tools/tank_replay.sh"
MK_TANK="$ROOT_DIR/tank_tools/mk_tank.sh"
ALIGN_CHANNELS="$ROOT_DIR/tank_tools/align_channels.sh"
BUILD_TANK_REPO="$ROOT_DIR/tank_tools/build_tank_repo.sh"
RUN_EVENTS="$ROOT_DIR/tank_tools/run_events.sh"
PARAMS_REAL="$ROOT_DIR/run_working_v8/params"

ew_bin() {
    if [ -n "${EW_BIN_OVERRIDE:-}" ]; then printf '%s\n' "$EW_BIN_OVERRIDE"; return; fi
    printf '%s/%s/bin\n' "${EW_HOME:-$ROOT_DIR}" "${EW_VERSION:-earthworm_8.0}"
}

have_bin() { [ -x "$(ew_bin)/$1" ]; }

tanksniff() { "$(ew_bin)/tanksniff" "$@"; }

# -----------------------------------------------------------------------------
#  Medicion de tanks (todo se deduce de la salida de tanksniff)
#    linea: STA.CHAN.NET.LOC (v v) pin tipo nsamp sps <stime> (start) <etime> (end) Nbytes
#    -> start = campo 4 entre parentesis, end = campo 6, SCNL = campo 1
# -----------------------------------------------------------------------------
tank_span() {  # <tank> -> "min_start max_end span" (epochs)
    tanksniff "$1" 2>/dev/null | awk '
        { n = split($0, p, /[()]/)
          if (n >= 6) { s = p[4] + 0; e = p[6] + 0
                        if (cnt == 0) { mins = s; maxe = e }
                        else { if (s < mins) mins = s; if (e > maxe) maxe = e }
                        cnt++ } }
        END { if (cnt == 0) exit 1
              printf "%.0f %.0f %.0f\n", mins, maxe, maxe - mins }'
}

# Igual que tank_span pero ademas el maximo starttime (lo que limita el futuro
# en wave_serverV: rechaza si starttime > now + 900; wave_serverV.c:3024).
tank_max_start() {  # <tank> -> epoch
    tanksniff "$1" 2>/dev/null | awk '
        { n = split($0, p, /[()]/)
          if (n >= 4) { s = p[4] + 0; if (cnt == 0 || s > maxs) maxs = s; cnt++ } }
        END { if (cnt == 0) exit 1; printf "%.0f\n", maxs }'
}

tank_msgcount() { tanksniff "$1" 2>/dev/null | wc -l | tr -d ' '; }

tank_scnls() { tanksniff "$1" 2>/dev/null | awk '{ print $1 }' | sort -u; }

# Ventana temporal de los SCNL cuyo canal empieza por <prefijo> (mismo criterio
# de prefijo que `tankcut -C`, remux_code.c:172). Necesario porque los canales de
# periodo largo (LH/LN) llegan mas lejos que los HH: usar el span global produce
# trozos vacios al final.
#   tank_span_chan <tank> <prefijo> -> "min_start max_end span"
tank_span_chan() {
    tanksniff "$1" 2>/dev/null | awk -v p="$2" '
        { n = split($0, parts, /[()]/)
          if (n < 6) next
          split($1, a, ".")
          if (p != "" && index(a[2], p) != 1) next
          s = parts[4] + 0; e = parts[6] + 0
          if (cnt == 0) { mi = s; ma = e } else { if (s < mi) mi = s; if (e > ma) ma = e }
          cnt++ }
        END { if (cnt == 0) exit 1
              printf "%.0f %.0f %.0f\n", mi, ma, ma - mi }'
}

tank_order_violations() {  # inversiones GLOBALES de endtime (criterio de "pauta")
    tanksniff "$1" 2>/dev/null | awk '
        { n = split($0, p, /[()]/)
          if (n >= 6) { e = p[6] + 0
                        if (seen && e < prev_e) bad++
                        prev_e = e; seen = 1 } }
        END { printf "%d\n", bad + 0 }'
}

tank_chan_violations() {  # inversiones de starttime DENTRO de un mismo SCNL
    tanksniff "$1" 2>/dev/null | awk '
        { key = $1; n = split($0, p, /[()]/)
          if (n >= 4) { t = p[4] + 0
                        if (key in last && t < last[key]) bad++
                        last[key] = t } }
        END { printf "%d\n", bad + 0 }'
}

check_order() {  # <tank> -> 0 reproducible con pauta | 2 no reproducible
    local g c
    g="$(tank_order_violations "$1")"
    c="$(tank_chan_violations "$1")"
    if [ "$g" -eq 0 ] && [ "$c" -eq 0 ]; then return 0; fi
    return 2
}

# Epoch <-> ISO8601 UTC (sin dependencias de zona horaria local).
epoch_to_iso() { date -u -d "@$1" +%Y-%m-%dT%H:%M:%SZ; }
epoch_to_stamp() { date -u -d "@$1" +%Y%m%d%H%M%S; }
iso_to_epoch() { date -u -d "$1" +%s; }

# -----------------------------------------------------------------------------
#  Fixtures / recortes
# -----------------------------------------------------------------------------
# Instante del sismo principal contenido en mseed/test2.mseed:
#   2015-09-16 22:54:33 UTC (Illapel M8.3).
# Los fixtures se anclan aqui (y no al minimo del tank) porque el tank no esta
# ordenado globalmente y su min-start cae en un hueco sin datos de 100 sps.
EVENT_EPOCH="${TANK_TEST_EVENT_EPOCH:-1442444073}"
FIXTURE_LEAD="${TANK_TEST_FIXTURE_LEAD:-60}"

# Corte anclado a un epoch absoluto: <master> <out> <epoch_inicio> <duracion> [extra tankcut]
tank_cut_at() {
    local master="$1" out="$2" start="$3" dur="$4"; shift 4
    local ts
    ts="$(epoch_to_stamp "$start")"
    "$(ew_bin)/tankcut" -s "$ts" -d "$dur" "$@" "$master" "$out" >/dev/null 2>&1
    [ -s "$out" ]
}

# Corte de una ventana relativa al inicio del tank:
#   <master> <out> <offset_segundos> <duracion> [args tankcut extra]
tank_cut() {
    local master="$1" out="$2" off="$3" dur="$4"; shift 4
    local vals min
    vals="$(tank_span "$master")" || return 1
    min="$(printf '%s' "$vals" | awk '{ print $1 }')"
    tank_cut_at "$master" "$out" "$(( min + off ))" "$dur" "$@"
}

# -----------------------------------------------------------------------------
#  Copia aislada de run_working_v8/params
#   make_params_copy <dst> <asis|hhz|bhz|chan:XXX|mismatch> [tanks_dir]
#  - alinea el codigo de canal en los TRES sitios coherentes:
#      estaciones_107.txt (col 3), pick_FP.sta (col 4) y wave_serverV.d (Tank)
#  - redirige los .tnk y TankStructFile a <tanks_dir> y los encoge a 1 MB / 20
#    entradas (evita preasignar ~1,4 GB por copia)
#  - activa Debug 1 en csnmags_toy.d (traza de ARC y de estacion resuelta)
#  - comenta las GUIs en startstop_replay.d (cadena headless: sin ventanas y sin
#    que csnhypodbp escriba csnhypodbp_hist.txt)
#  OJO: hay que comentar el par Process + Class/Priority. El parser de startstop
#  es una maquina de estados estricta: un Class/Priority sin su Process ->
#  "Expected: <Process>" y aborta sin crear los rings.
# -----------------------------------------------------------------------------
make_params_copy() {
    local dst="$1" mode="$2" tdir="${3:-}"
    local src="$PARAMS_REAL"
    local ALIGN_MODE MISMATCH
    case "$mode" in
        asis|hhz|bhz) ALIGN_MODE="$mode"; MISMATCH=0 ;;
        chan:*)       ALIGN_MODE="${mode#chan:}"; MISMATCH=0 ;;
        mismatch)     ALIGN_MODE="asis"; MISMATCH=1 ;;
        *) echo "modo invalido: $mode (usa asis|hhz|bhz|chan:XXX|mismatch)" >&2; return 2 ;;
    esac
    [ -d "$src" ] || { echo "no existe $src" >&2; return 3; }

    rm -rf "$dst"; mkdir -p "$dst"
    cp -a "$src/." "$dst/"
    [ -n "$tdir" ] || tdir="$dst/tanks"
    mkdir -p "$tdir"

    # Alineacion de canal: fuente unica de verdad -> tank_tools/align_channels.sh
    if [ "$MISMATCH" -eq 1 ]; then
        "$ALIGN_CHANNELS" --params-dir "$dst" --mode bhz --only estaciones --apply >/dev/null || true
        "$ALIGN_CHANNELS" --params-dir "$dst" --mode bhz --only tanks     --apply >/dev/null || true
        rm -f "$dst"/*.bak.* 2>/dev/null || true
    elif [ "$ALIGN_MODE" != "asis" ]; then
        "$ALIGN_CHANNELS" --params-dir "$dst" --mode "$ALIGN_MODE" --apply >/dev/null || true
        rm -f "$dst"/*.bak.* 2>/dev/null || true
    fi

    # wave_serverV.d: Tank STA CH NET LOC recSize INST MOD A B PATH
    #  - encoge los tanks (por defecto 1 MB / 20 entradas) para no preasignar
    #    ~1,4 GB por copia; se puede subir con PARAMS_TANK_MB / PARAMS_TANK_ENTRIES
    #  - redirige los .tnk y TankStructFile a <tanks_dir>
    local tank_mb="${PARAMS_TANK_MB:-1}"
    local tank_entries="${PARAMS_TANK_ENTRIES:-20}"
    awk -v tdir="$tdir" -v mb="$tank_mb" -v en="$tank_entries" '
        $1 == "Tank" && NF >= 11 {
            $9  = mb
            $10 = en
            $11 = tdir "/" $2 "_" $3 "_" $4 "_" $5 ".tnk"
            print; next
        }
        $1 == "TankStructFile" { print $1, tdir "/tank_struct.str"; next }
        { print }
    ' "$dst/wave_serverV.d" > "$dst/.wsv.tmp" && mv "$dst/.wsv.tmp" "$dst/wave_serverV.d"

    # csnmags_toy.d: Debug 1 (traza de ARC recibido y de estacion resuelta)
    if grep -qE '^[[:space:]]*Debug[[:space:]]' "$dst/csnmags_toy.d"; then
        sed -i -E 's/^([[:space:]]*Debug[[:space:]]+)[0-9]+/\11/' "$dst/csnmags_toy.d"
    else
        printf 'Debug           1\n' >> "$dst/csnmags_toy.d"
    fi

    # startstop_replay.d: cadena headless (sin GUIs, sin ventanas)
    awk -v guis="csntvp csnhypodbp csnrv csnstaevdisp ew_controller" '
        BEGIN { n = split(guis, a, " "); for (i = 1; i <= n; i++) g[a[i]] = 1 }
        {
            if (pend) {
                pend = 0
                if ($1 == "Class/Priority") { print "#" $0; next }
            }
            if ($1 == "Process") {
                cmd = $0
                sub(/^[^"]*"/, "", cmd); sub(/".*$/, "", cmd)
                split(cmd, c, " ")
                if (c[1] in g) { print "#" $0; pend = 1; next }
            }
            print
        }' "$dst/startstop_replay.d" > "$dst/.ss.tmp" && \
        mv "$dst/.ss.tmp" "$dst/startstop_replay.d"

    printf '%s\n' "$tdir"
}

# Parcha un parametro escalar en un .d de la copia aislada.
#   params_set <fichero> <clave> <valor>
params_set() {
    local f="$1" k="$2" v="$3"
    [ -f "$f" ] || return 1
    if grep -qE "^[[:space:]]*$k[[:space:]]" "$f"; then
        sed -i -E "s/^([[:space:]]*$k[[:space:]]+)[^[:space:]#]+/\\1$v/" "$f"
    else
        printf '%s %s\n' "$k" "$v" >> "$f"
    fi
}

# Lee un parametro escalar de un .d (primer match).
params_get() {
    local f="$1" k="$2"
    [ -f "$f" ] || return 1
    awk -v k="$k" '$1 == k { print $2; exit }' "$f"
}

# -----------------------------------------------------------------------------
#  Logs y rings
# -----------------------------------------------------------------------------
latest_log() { ls -t "$1/$2"_*.log 2>/dev/null | head -n1; }

# OJO: sniffring SIN -n DRENA el ring y solo muestra lo que llegue despues
# (sniffring.c:242). Con -n muestra lo que ya hay y luego se queda esperando,
# por eso se corta con timeout.
ring_dump() {  # <ring> [max_lineas]
    /usr/bin/timeout 10 "$(ew_bin)/sniffring" -n "$1" 2>/dev/null | head -n "${2:-400}"
}

ring_count_type() {  # <ring> <type>
    ring_dump "$1" 4000 | grep -c "type: *$2"
}

# -----------------------------------------------------------------------------
#  JSON minimo (sin dependencias)
# -----------------------------------------------------------------------------
json_str() {  # <texto> -> comillas + escapes
    printf '"%s"' "$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
}

# Cuenta lineas que casan con un patron. Devuelve SIEMPRE un entero (grep -c
# imprime "0" y sale con 1 cuando no hay coincidencias: un `|| echo 0` anadiria
# una segunda linea y romperia el JSON).
count_in() {  # <fichero> <patron ERE>
    local n
    n="$(grep -c -- "$2" "$1" 2>/dev/null)"
    case "$n" in ''|*[!0-9]*) n=0 ;; esac
    printf '%s' "$n"
}

# Lee un campo escalar de un manifest.json sin depender de python.
#   manifest_field <manifest> <clave>   -> valor sin comillas ni coma final
manifest_field() {  # <manifest> <clave>
    [ -f "$1" ] || return 1
    awk -v k="\"$2\"" '
        $0 ~ k {
            v = $0
            sub(/^[^:]*:[[:space:]]*/, "", v)
            gsub(/^"|",?[[:space:]]*$/, "", v)
            gsub(/,$/, "", v)
            print v
            exit
        }' "$1"
}
