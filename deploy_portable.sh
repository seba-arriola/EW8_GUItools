#!/bin/bash
# =============================================================================
#  deploy_portable.sh - replica incremental de EW8 a un directorio portable
#  Proyecto: EW8_GUItools
#
#  POR QUE EXISTE
#  --------------
#  Deja en DST (por defecto ~/ew8portable) SOLO lo necesario para levantar el
#  sistema en otra maquina, con TODAS las rutas relativas. Alli basta:
#
#       source ./ew8_unix.sh   &&   startstop
#
#  La copia es incremental (rsync) e idempotente: una segunda corrida sin
#  cambios no transfiere nada. Estructura espejo del repo:
#
#      ew8portable/
#        ew8_unix.sh                 (generado, sin rutas legacy absolutas)
#        ew8_gtk_env.sh              (copiado tal cual: entorno GTK4, fuente unica)
#        earthworm_8.0/bin/          (whitelist de binarios)
#        run_working_v8/params/      (whitelist + grids/ + tablas)
#        run_working_v8/log/         (vacio)
#        run_working_v8/tanks/       (vacio)
#
#  wave_serverV.d se GENERA con las rutas de tanks relativas (../tanks/...); no
#  se copia verbatim. Es valido porque startstop hace chdir(EW_PARAMS) y
#  wave_serverV abre las rutas tal cual (relativas al cwd).
#
#  USO
#  ---
#     ./deploy_portable.sh [--dst DIR] [--dry-run] [--verify-only]
#                          [--no-replay] [--no-monitor] [--no-delete] [-v] [-h]
#
#     --dry-run     muestra lo que haria, no escribe nada
#     --verify-only valida un DST ya existente y sale (0 ok, 2 fallo)
#     --no-delete   no purga obsoletos (omite --delete-excluded)
#     --no-replay   no copia startstop_replay.d / tankplayer.d.tmpl
#     --no-monitor  no copia ew_monitor.sh
#
#  Por defecto el portable es un espejo COMPLETO: incluye ew_monitor.sh y los
#  params de replay. Asi no puede divergir en silencio del repo (el monitor del
#  portable y el startstop_replay.d ausente eran exactamente eso). Los flags
#  viejos --with-replay/--with-monitor se aceptan por compatibilidad y ya no
#  cambian nada (son el comportamiento por defecto).
#
#  CODIGOS DE SALIDA
#     0  ok
#     1  error de uso / rsync
#     2  verificacion fallida (--verify-only o post-copia)
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR" && pwd)"

usage() {
    sed -n '2,/^# =====/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}
die() { echo "ERROR: $*" >&2; exit "$1"; }

DST="${EW8PORTABLE_DST:-$HOME/ew8portable}"
DRY=0
VERIFY_ONLY=0
DELETE=1
WITH_REPLAY=1
WITH_MONITOR=1
VERBOSE=0

case "${1:-}" in -h|--help) usage; exit 0 ;; esac
while [ $# -gt 0 ]; do
    case "$1" in
        --dst)         DST="${2:-}"; shift 2 ;;
        --dry-run)     DRY=1;        shift ;;
        --verify-only) VERIFY_ONLY=1; shift ;;
        --no-delete)   DELETE=0;     shift ;;
        --with-replay) WITH_REPLAY=1; shift ;;   # ya es el default (compat)
        --with-monitor) WITH_MONITOR=1; shift ;; # ya es el default (compat)
        --no-replay)   WITH_REPLAY=0; shift ;;
        --no-monitor)  WITH_MONITOR=0; shift ;;
        -v|--verbose)  VERBOSE=1;    shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; usage >&2; exit 1 ;;
    esac
done
[ -n "$DST" ] || die 1 "el destino no puede estar vacio"

DST_ABS="$(realpath -m "$DST")"
ROOT_ABS="$(realpath -m "$ROOT_DIR")"
case "$DST_ABS" in
    "$ROOT_ABS"|"$ROOT_ABS"/*) die 1 "el destino no puede estar dentro del repo: $DST_ABS" ;;
esac

SRC_BIN="$ROOT_DIR/earthworm_8.0/bin"
SRC_PARAMS="$ROOT_DIR/run_working_v8/params"
SRC_TANKS="$ROOT_DIR/run_working_v8/tanks"
SRC_MONITOR="$ROOT_DIR/ew_monitor.sh"
SRC_GTKENV="$ROOT_DIR/ew8_gtk_env.sh"

DST_BIN="$DST_ABS/earthworm_8.0/bin"
DST_PARAMS="$DST_ABS/run_working_v8/params"
DST_LOG="$DST_ABS/run_working_v8/log"
DST_TANKS="$DST_ABS/run_working_v8/tanks"
DST_ENV="$DST_ABS/ew8_unix.sh"
DST_GTKENV="$DST_ABS/ew8_gtk_env.sh"

# -----------------------------------------------------------------------------
#  Whitelists
# -----------------------------------------------------------------------------
BINS=(
    startstop slink2ew pick_FP pickS wave_serverV csnloc csnmags_toy
    csntvp csnhypodbp csnrv ew_controller csnstaevdisp
    hyp2000_ring nlloc_ring
    sniffring sniffrings sniffwave tanksniff remux_tbuf
    tankplayer ms2tank tankcut
)

PARAMS_FILES=(
    earthworm.d earthworm_global.d earthworm_commonvars.d startstop_unix.d
    slink2ew_HHZ.d pick_FP.d pickS.d csnloc.d csnmags_toy.d
    csntvp.d csnhypodbp.d csnrv.d ew_controller.d csnstaevdisp.d
    pick_FP.sta pickS.sta stations_to_view.sta estaciones_107.txt
    hyp2000_ring.hyp estaciones_hyp.sta ak135.crh chile_1d.crh
    N18-26_1.5k.crh N22-30_1.5k.crh N26-34_1.5k.crh
    N30-38_1.5k.crh N34-42_1.5k.crh N38-46_1.5k.crh
    spf_response.txt sp_distance.txt iasp91.tbl iasp91.hed
    world_map.jpg world_map_con_chile.jpg
)
if [ "$WITH_REPLAY" -eq 1 ]; then
    PARAMS_FILES+=( startstop_replay.d tankplayer.d.tmpl )
fi
# wave_serverV.d, hyp2000_ring.d y nlloc_ring.d se GENERAN (rutas relativas)
PARAMS_REQUIRED=( "${PARAMS_FILES[@]}" wave_serverV.d hyp2000_ring.d nlloc_ring.d )

# -----------------------------------------------------------------------------
#  Helpers
# -----------------------------------------------------------------------------
TOTAL_CHANGED=0
TOTAL_DELETED=0

run_rsync() {  # <srcdir> <dstdir> <filtros...>
    local src="$1" dst="$2"; shift 2
    local opts=(-a -i)
    [ "$DRY" -eq 1 ] && opts+=(-n)
    [ "$DELETE" -eq 1 ] && opts+=(--delete-excluded)
    [ "$DRY" -eq 0 ] && mkdir -p "$dst"
    local out
    if ! out="$(rsync "${opts[@]}" "$@" "$src/" "$dst/" 2>&1)"; then
        printf '%s\n' "$out" >&2
        die 1 "rsync fallo: $src -> $dst"
    fi
    [ "$VERBOSE" -eq 1 ] && printf '%s\n' "$out" | sed 's/^/    /'
    local nc nd
    nc="$(printf '%s\n' "$out" | grep -c '^>f' || true)"
    nd="$(printf '%s\n' "$out" | grep -c '^\*deleting' || true)"
    TOTAL_CHANGED=$(( TOTAL_CHANGED + nc ))
    TOTAL_DELETED=$(( TOTAL_DELETED + nd ))
}

write_if_changed() {  # <fichero>   (contenido por stdin)
    local f="$1" tmp="$1.tmp.$$"
    mkdir -p "$(dirname "$f")"
    cat > "$tmp"
    if [ -f "$f" ] && cmp -s "$tmp" "$f"; then
        rm -f "$tmp"
    else
        mv -f "$tmp" "$f"
    fi
}

gen_env() {
    cat <<'EOF'
#!/bin/bash
# =============================================================================
#  ew8_unix.sh - entorno portable EW8 (autogenerado por deploy_portable.sh)
#  Todas las rutas son relativas al directorio de este script.
# =============================================================================
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

export EW_INSTALLATION="INST_UNKNOWN"
export EW_HOME="${DIR}"
export EW_VERSION="earthworm_8.0"
export EW_PARAMS="${DIR}/run_working_v8/params"
export EW_LOG="${DIR}/run_working_v8/log"
export SYS_NAME="$(hostname)"
export PATH="${DIR}/${EW_VERSION}/bin:$PATH"

# GTK4 (WSLg): renderer y backend. Fuente UNICA compartida con el repo; el
# deploy copia ew8_gtk_env.sh tal cual junto a este script.
if [ -r "${DIR}/ew8_gtk_env.sh" ]; then
    . "${DIR}/ew8_gtk_env.sh"
else
    echo "AVISO: falta ${DIR}/ew8_gtk_env.sh (entorno GTK4 no aplicado)" >&2
fi

echo "EW8 portable:"
echo " - HOME    : $EW_HOME"
echo " - Params  : $EW_PARAMS"
echo " - Logs    : $EW_LOG"
echo " - Binarios: ${DIR}/${EW_VERSION}/bin"

chmod +x "${DIR}/${EW_VERSION}"/bin/* 2>/dev/null || true
cd "${DIR}"
EOF
}

gen_wsv() {
    local prefix="$SRC_TANKS/"
    [ -f "$SRC_PARAMS/wave_serverV.d" ] || die 1 "no existe $SRC_PARAMS/wave_serverV.d"
    sed "s#${prefix}#../tanks/#g" "$SRC_PARAMS/wave_serverV.d" \
        | write_if_changed "$DST_PARAMS/wave_serverV.d"
}

# hyp2000_ring.d / nlloc_ring.d apuntan a tmp/ con ruta absoluta del repo; se
# regeneran con ../../tmp/... (relativo a params/) para que el arbol sea portable.
gen_ring_d() {  # <fichero>
    local f="$1"
    [ -f "$SRC_PARAMS/$f" ] || die 1 "no existe $SRC_PARAMS/$f"
    sed "s#${ROOT_ABS}/#../../#g" "$SRC_PARAMS/$f" \
        | write_if_changed "$DST_PARAMS/$f"
}

verify() {
    local fail=0
    local b p
    for b in "${BINS[@]}"; do
        [ -x "$DST_BIN/$b" ] || { echo "  FALTA/no-ejecutable: bin/$b"; fail=1; }
    done
    for p in "${PARAMS_REQUIRED[@]}"; do
        [ -f "$DST_PARAMS/$p" ] || { echo "  FALTA: params/$p"; fail=1; }
    done
    if ! compgen -G "$DST_PARAMS/grids/*.grid" >/dev/null 2>&1; then
        echo "  FALTAN: params/grids/*.grid"; fail=1
    fi
    if grep -RIn -- '/home/' "$DST_PARAMS" >/dev/null 2>&1; then
        echo "  RUTA ABSOLUTA (/home/) encontrada en params/"; fail=1
    fi
    if ! grep -qE '^TankStructFile[[:space:]]+\.\./tanks/' "$DST_PARAMS/wave_serverV.d" 2>/dev/null; then
        echo "  TankStructFile no es relativo (../tanks/)"; fail=1
    fi
    if awk '$1=="Tank" && $11 ~ /^\// {bad=1} END{exit (bad?0:1)}' \
            "$DST_PARAMS/wave_serverV.d" 2>/dev/null; then
        echo "  hay lineas Tank con ruta absoluta"; fail=1
    fi
    if [ ! -f "$DST_ENV" ]; then
        echo "  FALTA: ew8_unix.sh"; fail=1
    elif ! bash -n "$DST_ENV" 2>/dev/null; then
        echo "  ew8_unix.sh invalido (bash -n)"; fail=1
    fi
    if [ ! -f "$DST_GTKENV" ]; then
        echo "  FALTA: ew8_gtk_env.sh"; fail=1
    fi
    # Guardia de regresion (incidente 2026-10-08): el env generado perdia en
    # silencio el entorno GTK4 (avisos libEGL/MESA + decoracion distinta). Se
    # comprueba de forma FUNCIONAL y determinista: `source` del env desplegado en
    # dos escenarios (con y sin DISPLAY) y lectura de las variables resultantes.
    # DISPLAY se fija a proposito para no depender del entorno de quien despliega.
    local gtk_x gtk_w
    gtk_x="$(DISPLAY=:0 bash -c "source '$DST_ENV' >/dev/null 2>&1; printf '%s|%s' \"\${GSK_RENDERER:-}\" \"\${GDK_BACKEND:-}\"" 2>/dev/null || true)"
    gtk_w="$(env -u DISPLAY bash -c "source '$DST_ENV' >/dev/null 2>&1; printf '%s|%s' \"\${GSK_RENDERER:-}\" \"\${GDK_BACKEND:-}\"" 2>/dev/null || true)"
    if [ "$gtk_x" != "cairo|x11" ]; then
        echo "  con DISPLAY, ew8_unix.sh no exporta el entorno GTK4 (='$gtk_x', esperado 'cairo|x11')"
        fail=1
    fi
    if [ "$gtk_w" != "cairo|" ]; then
        echo "  sin DISPLAY, ew8_unix.sh no deja Wayland (='$gtk_w', esperado 'cairo|')"
        fail=1
    fi
    if [ "$WITH_MONITOR" -eq 1 ]; then
        if [ ! -x "$DST_ABS/ew_monitor.sh" ]; then
            echo "  FALTA/no-ejecutable: ew_monitor.sh"; fail=1
        elif [ -f "$SRC_MONITOR" ] && ! cmp -s "$SRC_MONITOR" "$DST_ABS/ew_monitor.sh"; then
            echo "  ew_monitor.sh difiere del repo (debe ser copia literal)"; fail=1
        fi
    fi
    return $fail
}

# -----------------------------------------------------------------------------
#  Main
# -----------------------------------------------------------------------------
echo "[deploy] origen : $ROOT_DIR"
echo "[deploy] destino: $DST_ABS"
if [ "$VERIFY_ONLY" -eq 1 ]; then
    echo "[deploy] accion : verificar"
else
    [ "$DRY" -eq 1 ] && echo "[deploy] accion : dry-run" || echo "[deploy] accion : copiar"
fi
echo

BIN_INC=()
for b in "${BINS[@]}"; do BIN_INC+=(--include="/$b"); done
BIN_INC+=(--include='*/' --exclude='*')

PAR_INC=()
PAR_INC+=(--exclude='/_legacy_atwc/' --exclude='/hyp2000_output/' --exclude='/response/')
for p in "${PARAMS_FILES[@]}"; do PAR_INC+=(--include="/$p"); done
PAR_INC+=(--include='/grids/***')
PAR_INC+=(--include='*/')
PAR_INC+=(--filter='P /wave_serverV.d')   # generado aparte, no borrar
PAR_INC+=(--filter='P /hyp2000_ring.d')   # generado (rutas relativas)
PAR_INC+=(--filter='P /nlloc_ring.d')     # generado (rutas relativas)
# Estado en vivo de los modulos: slink2ew crea `slink<id>.state` (posicion de
# SeedLink) y pick_FP `pick_FP_<id>.ndx` (indice de picks) EN params/ mientras
# operan. Se excluyen de la copia pero hay que PROTEGERLOS de la purga: borrarlos
# con el sistema arriba le quita a slink2ew su posicion y a pick_FP su indice.
PAR_INC+=(--filter='P *.state')
PAR_INC+=(--filter='P *.ndx')
PAR_INC+=(--filter='P *.queue')
PAR_INC+=(--exclude='*')

if [ "$VERIFY_ONLY" -eq 1 ]; then
    if verify; then echo "[deploy] verificacion OK"; exit 0; else echo "[deploy] verificacion FALLIDA"; exit 2; fi
fi

if [ "$DRY" -eq 0 ]; then
    mkdir -p "$DST_BIN" "$DST_PARAMS" "$DST_LOG" "$DST_TANKS"
fi

run_rsync "$SRC_BIN"    "$DST_BIN"    "${BIN_INC[@]}"
run_rsync "$SRC_PARAMS" "$DST_PARAMS" "${PAR_INC[@]}"

if [ "$DRY" -eq 0 ]; then
    gen_env | write_if_changed "$DST_ENV"
    chmod +x "$DST_ENV" 2>/dev/null || true
    # Entorno GTK4: se copia tal cual (fuente unica; NO se regenera).
    [ -f "$SRC_GTKENV" ] || die 1 "no existe $SRC_GTKENV"
    write_if_changed "$DST_GTKENV" < "$SRC_GTKENV"
    gen_wsv
    gen_ring_d hyp2000_ring.d
    gen_ring_d nlloc_ring.d
    if [ "$WITH_MONITOR" -eq 1 ]; then
        [ -f "$SRC_MONITOR" ] || die 1 "no existe $SRC_MONITOR"
        # Copia literal: el monitor del portable debe ser identico al del repo.
        write_if_changed "$DST_ABS/ew_monitor.sh" < "$SRC_MONITOR"
        chmod +x "$DST_ABS/ew_monitor.sh" 2>/dev/null || true
    fi
fi

echo "[deploy] cambiados=$TOTAL_CHANGED eliminados=$TOTAL_DELETED"

if [ "$DRY" -eq 1 ]; then
    echo "[deploy] dry-run: no se escribio nada."
    exit 0
fi

if verify; then
    echo "[deploy] verificacion OK"
    exit 0
else
    echo "[deploy] verificacion FALLIDA"
    exit 2
fi
