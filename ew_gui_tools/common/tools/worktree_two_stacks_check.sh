#!/bin/sh
# =============================================================================
#  worktree_two_stacks_check.sh - comprueba que DOS stacks vivos NO se pisan.
#
#  Requiere los dos arriba (el repo principal y un worktree bootstrapeado con
#  --instance N>=1). No arranca ni para nada: solo mira. Ver docs/WORKTREES.md.
#
#  USO
#    worktree_two_stacks_check.sh <worktree> [--sniff]
#      --sniff   ademas muestrea el trafico de los PICK_RING de cada instancia
#                (informativo; el no-cross-talk lo prueba `ipcs -m`, CA-6).
#
#  exit: 0 todo OK · 2 uso · 3 entorno · 4 hay CA que fallan
# =============================================================================
set -u

prog=worktree_two_stacks_check
die()  { c=$1; shift; printf '%s: %s\n' "$prog" "$*" >&2; exit "$c"; }
say()  { printf '%s\n' "$*"; }

inject=0
WT=""
for a in "$@"; do
    case "$a" in
        --sniff)  inject=1 ;;
        -*)       die 2 "opcion desconocida: $a" ;;
        *)        [ -z "$WT" ] || die 2 "sobra un argumento: $a"; WT="$a" ;;
    esac
done
[ -n "$WT" ] || die 2 "falta <worktree>"
[ -d "$WT" ] || die 3 "no existe $WT"
WT="$(CDPATH= cd -- "$WT" && pwd)"
common="$(git -C "$WT" rev-parse --git-common-dir 2>/dev/null)" || die 3 "$WT no es un repo git"
case "$common" in /*) ;; *) common="$WT/$common" ;; esac
MAIN="$(CDPATH= cd -- "$common/.." && pwd)"

rc=0
ck()  { printf 'CHECK  %s\n' "$*"; }
bad() { printf 'FALLA  %s\n' "$*" >&2; rc=4; }

keys_of() {   # keys_of <params dir>
    python3 - "$1/earthworm.d" <<'PY'
import re, sys
try:
    ks = [int(m.group(1)) for m in (re.match(r'^\s*Ring\s+\S+\s+(\d+)', l) for l in open(sys.argv[1])) if m]
except OSError:
    ks = []
print(' '.join(str(k) for k in ks))
PY
}

P_A="$MAIN/run_working_v8/params"
P_B="$WT/run_working_v8/params"
[ -f "$P_A/earthworm.d" ] && [ -f "$P_B/earthworm.d" ] || die 3 "falta earthworm.d en alguna instancia"

# --- CA-1: keys disjuntas ----------------------------------------------------
KA="$(keys_of "$P_A")"
KB="$(keys_of "$P_B")"
if [ -z "$KA" ] || [ -z "$KB" ]; then
    bad "no puedo leer las keys de anillo de las dos instancias"
else
    ov="$(python3 - "$KA" "$KB" <<'PY'
import sys
a = set(sys.argv[1].split()); b = set(sys.argv[2].split())
print(' '.join(sorted(a & b)))
PY
)"
    if [ -n "$ov" ]; then
        bad "CA-1: keys compartidas entre las dos instancias: $ov"
    else
        ck "CA-1 keys disjuntas (A: $(printf '%s' "$KA" | tr ' ' '\n' | wc -l) claves, B: $(printf '%s' "$KB" | tr ' ' '\n' | wc -l))"
    fi
fi

# --- CA-6 (estructural): cada anillo tiene DOS segmentos, uno por instancia ---
# Si las dos instancias compartieran las keys habria UN solo segmento por anillo
# y cualquier mensaje de A lo leerian los consumidores de B (cross-talk). Con
# keys propias tienen que existir los DOS segmentos: eso lo dice `ipcs -m`.
both="$(python3 - "$KA" "$KB" <<'PY'
import subprocess, sys
try:
    out = subprocess.run(['ipcs', '-m'], capture_output=True, text=True).stdout
except OSError:
    print('na'); sys.exit()
live = set()
for l in out.splitlines():
    if l.startswith('0x'):
        try:
            live.add(int(l.split()[0], 16))
        except ValueError:
            pass
a = set(int(x) for x in sys.argv[1].split())
b = set(int(x) for x in sys.argv[2].split())
la, lb = a & live, b & live
print('%d %d %d %d' % (len(la), len(lb), len((la | lb)) - len(la) - len(lb), len(la | lb)))
PY
)"
set -- $both
if [ "${1:-na}" = na ]; then
    say "nota: no hay ipcs: CA-6 no comprobado"
elif [ "$1" -gt 0 ] && [ "$2" -gt 0 ] && [ "$3" -eq 0 ]; then
    ck "CA-6 segmentos vivos A=$1 B=$2, ninguno compartido ($4 keys vivas en total): un mensaje de A no puede caer en el ring de B"
else
    bad "CA-6 segmentos vivos A=$1 B=$2 (compartidos=$3, distintas=$4): ¿stack sin arrancar o keys compartidas?"
fi

# --- CA-2/CA-3: cada `status` ve SOLO su stack ------------------------------
st() {   # st <repo> -> "alive=<n> pids=<...>" + el pid de startstop
    ( cd "$1" && bash ew_monitor.sh status 2>&1 )
}
sa="$(st "$MAIN")"; sb="$(st "$WT")"
pa="$(printf '%s' "$sa" | sed -n 's/.*startstop pid \([0-9]*\).*/\1/p' | head -1)"
pb="$(printf '%s' "$sb" | sed -n 's/.*startstop pid \([0-9]*\).*/\1/p' | head -1)"
na="$(printf '%s' "$sa" | sed -n 's/.*Alive:\([0-9]*\).*/\1/p' | head -1)"
nb="$(printf '%s' "$sb" | sed -n 's/.*Alive:\([0-9]*\).*/\1/p' | head -1)"
[ -n "$pa" ] && [ -n "$pb" ] || bad "alguno de los dos stacks no esta arriba (A=$pa B=$pb)"
if [ -n "$pa" ] && [ -n "$pb" ] && [ "$pa" != "$pb" ]; then ck "CA-2 startstop distintos (A=$pa B=$pb)"; fi
[ -n "$na" ] && [ -n "$nb" ] && ck "CA-2 modulos Alive (A=$na B=$nb)"

# interseccion de pids vistos por cada status (deberia ser vacia)
pids_a="$(printf '%s' "$sa" | grep -oE 'pid [0-9]+' | awk '{print $2}' | sort -u | tr '\n' ' ')"
pids_b="$(printf '%s' "$sb" | grep -oE 'pid [0-9]+' | awk '{print $2}' | sort -u | tr '\n' ' ')"
shared="$(python3 - "$pids_a" "$pids_b" <<'PY'
import sys
a = set(sys.argv[1].split()); b = set(sys.argv[2].split())
print(' '.join(sorted(a & b)))
PY
)"
if [ -n "$shared" ]; then bad "CA-2: los dos status comparten pids: $shared"; else ck "CA-2 ningun pid compartido entre los dos status"; fi

# --- CA-4: diag process acotado ---------------------------------------------
for r in "$MAIN" "$WT"; do
    n="$(cd "$r" && sh ew_gui_tools/common/tools/diag.sh process 2>&1 | grep -c '^ *[0-9]' )"
    ck "CA-4 diag process en $(basename "$r"): $n procesos"
done

# --- CA-5: dos contenedores de BD -------------------------------------------
if command -v docker >/dev/null 2>&1; then
    for p in 5435 5436; do
        own="$(docker ps --filter "publish=$p" --format '{{.Names}}' 2>/dev/null | head -1)"
        [ -n "$own" ] && ck "CA-5 puerto $p -> contenedor $own" || bad "CA-5 nadie escucha en $p"
    done
fi

# --- CA-7/CA-8: nada escrito fuera (centinela) -----------------------------
SENT=/tmp/opencode/two_stacks_sentinel
rm -f "$SENT"; touch "$SENT"; sleep 1
if [ "$inject" = 1 ]; then
    SNIFF="$MAIN/ew_8.0/bin/sniffring"
    if [ -x "$SNIFF" ]; then
        # Informativo: se ve el trafico de cada PICK_RING. La prueba ESTRUCTURAL
        # (CA-6, ipcs) es la que garantiza que no hay cross-talk; `putpick` queda
        # fuera porque su pickfile no esta documentado en el repo.
        for pair in "A:$MAIN" "B:$WT"; do
            tag=${pair%%:*}; repo=${pair#*:}
            n="$( ( cd "$repo/run_working_v8/params" && /usr/bin/timeout 5 "$SNIFF" -n PICK_RING ) 2>/dev/null | grep -c 'Received' )"
            ck "CA-6 (info) PICK_RING de $tag: $n mensajes"
        done
    fi
else
    say "nota: --sniff omitido: no se muestrea el trafico de los PICK_RING"
fi

echo "-- ficheros nuevos (desde el centinela) --"
echo "-- /mnt/d/resources --"
find /mnt/d/resources/nlloc /mnt/d/resources/hyp2000 /mnt/d/resources/profiles /mnt/d/resources/overlays \
     -newer "$SENT" -printf '%p\n' 2>/dev/null | head -10
echo "-- params de A (desde el centinela) --"
find "$P_A" -newer "$SENT" -printf '%p\n' 2>/dev/null | head -10
echo "-- params de B (desde el centinela) --"
find "$P_B" -newer "$SENT" -printf '%p\n' 2>/dev/null | head -10

# --- CA-10: el cwd de los refinadores, en su worktree -----------------------
echo "-- cwd de los refinadores (resuelto POR cwd: hay dos de cada nombre) --"
for r in "$MAIN" "$WT"; do
    for m in hyp2000_ring nlloc_ring; do
        found=0
        for p in $(pgrep -x "$m" 2>/dev/null); do
            c="$(readlink "/proc/$p/cwd" 2>/dev/null)"
            case "$c" in "$r"/*) found=$((found + 1)) ;; esac
        done
        if [ "$found" -ge 1 ]; then
            ck "CA-10 $m en $(basename "$r"): $found proceso(s) con cwd dentro de su worktree"
        else
            bad "CA-10 $m: ningun proceso con cwd dentro de $r"
        fi
    done
done

[ "$rc" -eq 0 ] && say "$prog: OK"
exit "$rc"
