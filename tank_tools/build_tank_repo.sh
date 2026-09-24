#!/bin/bash
# =============================================================================
#  build_tank_repo.sh - construye el "repo de tanks" a partir de mseed/
#  Proyecto: EW8_GUItools
#
#  POR QUE
#  -------
#  Convertir miniSEED a tank es lo caro (ms2tank + remux_tbuf: ~36 s por 280 MB).
#  Se hace UNA vez y se reutiliza en todas las pruebas. El resultado queda en
#  tank_repo/<slug>/ con:
#     master.tank     conversion completa y reproducible con pauta correcta
#     manifest.json   span, inventario de SCNL, nmsgs, canales, si hizo falta remux
#  El manifest es lo que hace que `run_events.sh plan` sea instantaneo: evita
#  volver a recorrer el tank con tanksniff.
#
#  USO
#  ---
#     ./tank_tools/build_tank_repo.sh [--src DIR] [--repo DIR] [--nsamp N]
#                                     [--only GLOB] [--force] [--no-remux]
#                                     [--dry-run] [-h]
#
#  REANUDABLE: si ya existe <repo>/<slug>/manifest.json, se salta (usa --force
#  para rehacerlo).
#
#  SOBRE --remux (importante)
#  --------------------------
#  `tankplayer` pauta con Ptime = endtime de cada paquete. Si un paquete tiene
#  endtime anterior al previo, su espera sale negativa y se emite de inmediato:
#  la reproduccion deja de ser fiel. ms2tank NO garantiza ese orden. Por eso
#  este script llama primero a mk_tank.sh SIN --remux y, si mk_tank.sh sale con
#  2 (no reproducible), repite con --remux. Asi no se paga el remux cuando no
#  hace falta.
#
#  CODIGOS DE SALIDA
#     0  ok
#     1  error de uso
#     2  al menos un fichero fallo
#     3  faltan binarios o directorios
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tank_lib.sh
source "$SCRIPT_DIR/tank_lib.sh"

SRC="$MSEED_DIR"
REPO="$TANK_REPO"
NSAMP="1008"
ONLY="*"
FORCE=0
NOREMUX=0
DRYRUN=0

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die() { echo "ERROR: $*" >&2; exit 1; }

case "${1:-}" in -h|--help) usage; exit 0 ;; esac

while [ $# -gt 0 ]; do
    case "$1" in
        --src)      SRC="${2:-}";    shift 2 ;;
        --repo)     REPO="${2:-}";   shift 2 ;;
        --nsamp)    NSAMP="${2:-}";  shift 2 ;;
        --only)     ONLY="${2:-}";   shift 2 ;;
        --force)    FORCE=1;         shift ;;
        --no-remux) NOREMUX=1;       shift ;;
        --dry-run)  DRYRUN=1;        shift ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; usage >&2; exit 1 ;;
    esac
done

[ -d "$SRC" ]  || die "no existe el directorio de miniSEED: $SRC"
[ -x "$MK_TANK" ] || die "no existe $MK_TANK"
have_bin tanksniff || die "no existe $(ew_bin)/tanksniff"

case "$NSAMP" in ''|*[!0-9]*) die "--nsamp debe ser un entero" ;; esac
[ "$NSAMP" -ge 1 ] && [ "$NSAMP" -le 1008 ] || die "--nsamp debe estar entre 1 y 1008"

mkdir -p "$REPO"

# --- inventario de entrada ----------------------------------------------------
files=()
while IFS= read -r f; do files+=( "$f" ); done \
    < <(find "$SRC" -maxdepth 1 -type f -name '*.mseed' -name "$ONLY" | sort)

if [ "${#files[@]}" -eq 0 ]; then
    die "no hay ficheros *.mseed en $SRC (filtro --only '$ONLY')"
fi

echo "[repo] origen  : $SRC"
echo "[repo] destino : $REPO"
echo "[repo] nsamp   : $NSAMP"
echo "[repo] ficheros: ${#files[@]}"
echo

# --- manifiesto de un tank ya construido --------------------------------------
# Genera <repo>/<slug>/manifest.json a partir del master.tank.
write_manifest() {
    local slug="$1" tank="$2" src="$3" remux="$4" tmp="$5"
    local manifest="$REPO/$slug/manifest.json"
    local sniff="$tmp/sniff.txt"

    tanksniff "$tank" > "$sniff" 2>/dev/null || return 1

    local nmsgs mins maxe span maxs gviol cviol nscnl nsta
    nmsgs="$(wc -l < "$sniff" | tr -d ' ')"
    read -r mins maxe span <<EOF
$(awk '
    { n = split($0, p, /[()]/)
      if (n >= 6) { s = p[4] + 0; e = p[6] + 0
                    if (cnt == 0) { mi = s; ma = e } else { if (s < mi) mi = s; if (e > ma) ma = e }
                    cnt++ } }
    END { printf "%.0f %.0f %.0f\n", mi + 0, ma + 0, ma - mi }' "$sniff")
EOF
    maxs="$(awk '
        { n = split($0, p, /[()]/)
          if (n >= 4) { s = p[4] + 0; if (cnt == 0 || s > mx) mx = s; cnt++ } }
        END { printf "%.0f\n", mx + 0 }' "$sniff")"
    gviol="$(awk '
        { n = split($0, p, /[()]/)
          if (n >= 6) { e = p[6] + 0; if (seen && e < prev) bad++; prev = e; seen = 1 } }
        END { printf "%d", bad + 0 }' "$sniff")"
    cviol="$(awk '
        { k = $1; n = split($0, p, /[()]/)
          if (n >= 4) { t = p[4] + 0; if (k in last && t < last[k]) bad++; last[k] = t } }
        END { printf "%d", bad + 0 }' "$sniff")"
    nscnl="$(awk '{ print $1 }' "$sniff" | sort -u | wc -l | tr -d ' ')"
    nsta="$(awk '{ n = split($1, a, "."); print a[1] }' "$sniff" | sort -u | wc -l | tr -d ' ')"
    local chans nets locs hhz_scnl
    chans="$(awk '{ n = split($1, a, "."); print a[2] }' "$sniff" | sort -u | paste -sd, -)"
    nets="$(awk  '{ n = split($1, a, "."); print a[3] }' "$sniff" | sort -u | paste -sd, -)"
    locs="$(awk  '{ n = split($1, a, "."); print a[4] }' "$sniff" | sort -u | paste -sd, -)"
    hhz_scnl="$(awk '{ n = split($1, a, "."); if (a[2] == "HHZ") print $1 }' "$sniff" | sort -u | wc -l | tr -d ' ')"

    # Ventana de los canales HH: los de periodo largo llegan mas lejos, asi que el
    # span global no sirve para trocear (dejaria trozos vacios al final).
    local hh_mins hh_maxe hh_span
    read -r hh_mins hh_maxe hh_span <<EOF
$(awk '
    { n = split($0, parts, /[()]/); if (n < 6) next
      split($1, a, "."); if (index(a[2], "HH") != 1) next
      s = parts[4] + 0; e = parts[6] + 0
      if (c == 0) { mi = s; ma = e } else { if (s < mi) mi = s; if (e > ma) ma = e }
      c++ }
    END { printf "%.0f %.0f %.0f\n", mi + 0, ma + 0, ma - mi }' "$sniff")
EOF

    local src_bytes tank_bytes built
    src_bytes="$(stat -c %s "$src" 2>/dev/null || echo 0)"
    tank_bytes="$(stat -c %s "$tank" 2>/dev/null || echo 0)"
    built="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    {
        printf '{\n'
        printf '  "slug": %s,\n'            "$(json_str "$slug")"
        printf '  "source": %s,\n'          "$(json_str "$src")"
        printf '  "source_bytes": %s,\n'    "$src_bytes"
        printf '  "tank": %s,\n'            "$(json_str "$tank")"
        printf '  "tank_bytes": %s,\n'      "$tank_bytes"
        printf '  "nsamp": %s,\n'           "$NSAMP"
        printf '  "remux": %s,\n'           "$remux"
        printf '  "nmsgs": %s,\n'           "$nmsgs"
        printf '  "start_epoch": %s,\n'     "$mins"
        printf '  "end_epoch": %s,\n'       "$maxe"
        printf '  "max_start_epoch": %s,\n' "$maxs"
        printf '  "start_utc": %s,\n'       "$(json_str "$(epoch_to_iso "$mins")")"
        printf '  "end_utc": %s,\n'         "$(json_str "$(epoch_to_iso "$maxe")")"
        printf '  "span_s": %s,\n'          "$span"
        printf '  "n_scnl": %s,\n'          "$nscnl"
        printf '  "n_sta": %s,\n'           "$nsta"
        printf '  "hhz_scnl": %s,\n'        "$hhz_scnl"
        printf '  "hh_start_epoch": %s,\n'  "$hh_mins"
        printf '  "hh_end_epoch": %s,\n'    "$hh_maxe"
        printf '  "hh_span_s": %s,\n'       "$hh_span"
        printf '  "chans": %s,\n'           "$(json_str "$chans")"
        printf '  "nets": %s,\n'            "$(json_str "$nets")"
        printf '  "locs": %s,\n'            "$(json_str "$locs")"
        printf '  "endtime_violations": %s,\n' "$gviol"
        printf '  "perchan_violations": %s,\n' "$cviol"
        printf '  "playable": %s,\n'        "$([ "$gviol" -eq 0 ] && [ "$cviol" -eq 0 ] && echo true || echo false)"
        printf '  "built_at_utc": %s\n'      "$(json_str "$built")"
        printf '}\n'
    } > "$manifest"
    rm -f "$sniff"
    printf '%s\n' "$manifest"
}

# --- bucle principal ----------------------------------------------------------
n_ok=0; n_skip=0; n_fail=0
declare -a FAILED=()
t_start="$(date +%s)"

for f in "${files[@]}"; do
    base="$(basename "$f")"
    slug="${base%.mseed}"
    dest="$REPO/$slug"
    i=$(( n_ok + n_skip + n_fail + 1 ))

    if [ -f "$dest/manifest.json" ] && [ "$FORCE" -eq 0 ]; then
        echo "[repo] ($i/${#files[@]}) SKIP  $slug (ya existe manifest.json)"
        n_skip=$(( n_skip + 1 ))
        continue
    fi

    if [ "$DRYRUN" -eq 1 ]; then
        echo "[repo] ($i/${#files[@]}) DRY   $slug <- $base ($(stat -c %s "$f" 2>/dev/null) bytes)"
        continue
    fi

    echo "[repo] ($i/${#files[@]}) BUILD $slug <- $base"
    tmp="$REPO/.build/$slug"
    rm -rf "$tmp"; mkdir -p "$tmp"

    rc=0
    log="$tmp/build1.log"
    "$MK_TANK" "$f" "$tmp" --nsamp "$NSAMP" > "$log" 2>&1 || rc=$?
    remux="false"
    if [ "$rc" -eq 2 ]; then
        if [ "$NOREMUX" -eq 1 ]; then
            echo "       ERROR: necesita --remux (no reproducible) y se paso --no-remux" >&2
            n_fail=$(( n_fail + 1 )); FAILED+=("$slug: requiere --remux")
            rm -rf "$tmp"; continue
        fi
        echo "       no reproducible con pauta correcta -> reintentando con --remux"
        rc=0
        log="$tmp/build2.log"
        "$MK_TANK" "$f" "$tmp" --nsamp "$NSAMP" --remux > "$log" 2>&1 || rc=$?
        remux="true"
    fi
    if [ "$rc" -ne 0 ]; then
        echo "       ERROR: mk_tank.sh salio con $rc (ver $log)" >&2
        tail -n 3 "$log" 2>/dev/null | sed 's/^/         /' >&2
        n_fail=$(( n_fail + 1 )); FAILED+=("$slug: mk_tank rc=$rc")
        rm -rf "$tmp"; continue
    fi
    if [ ! -s "$tmp/master.tank" ]; then
        echo "       ERROR: no se genero master.tank" >&2
        n_fail=$(( n_fail + 1 )); FAILED+=("$slug: sin master.tank")
        rm -rf "$tmp"; continue
    fi

    mkdir -p "$dest"
    mv -f "$tmp/master.tank" "$dest/master.tank"

    if mf="$(write_manifest "$slug" "$dest/master.tank" "$f" "$remux" "$tmp")"; then
        nmsgs="$(awk -F': ' '$1 ~ /"nmsgs"/ { gsub(/,/, "", $2); print $2 }' "$dest/manifest.json")"
        span_s="$(awk -F': ' '$1 ~ /"span_s"/ { gsub(/,/, "", $2); print $2 }' "$dest/manifest.json")"
        echo "       OK  $(du -h "$dest/master.tank" | cut -f1)  nmsgs=$nmsgs  span=${span_s}s  remux=$remux"
        n_ok=$(( n_ok + 1 ))
    else
        echo "       ERROR: no se pudo leer el tank con tanksniff" >&2
        n_fail=$(( n_fail + 1 )); FAILED+=("$slug: tanksniff fallo")
    fi
    rm -rf "$tmp"
done

# --- resumen ------------------------------------------------------------------
t_end="$(date +%s)"
echo
echo "[repo] listo en $(( t_end - t_start )) s"
echo "       construidos: $n_ok"
echo "       saltados   : $n_skip"
echo "       fallidos   : $n_fail"
if [ "$n_fail" -gt 0 ]; then
    echo "       fallos:"
    for x in "${FAILED[@]}"; do echo "         - $x"; done
    exit 2
fi
exit 0
