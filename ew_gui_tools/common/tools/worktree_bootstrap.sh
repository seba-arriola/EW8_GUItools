#!/bin/sh
# =============================================================================
#  worktree_bootstrap.sh - prepara un worktree (o el repo principal) como
#  INSTANCIA: genera run_working_v8/params/earthworm.d a partir de
#  earthworm.d.base con las keys de los ANILLOS offseteadas (+10000*N) y deja
#  db.env con el puerto de esa instancia (5435+N).
#
#  NO crea directorios de trabajo ni symlinks: cada modulo crea los suyos en
#  runtime (ver AGENTS.md §7 y docs/PLANworktree.md).
#
#  USO
#  ---
#    worktree_bootstrap.sh [<carpeta>] [--instance N] [--check] [--refresh]
#                          [--print-plan] [--force]
#
#      <carpeta>     raiz del worktree (default: el directorio actual)
#      --instance N  instancia. N=0 esta reservado al repo PRINCIPAL; usarlo en
#                    un worktree exige --force. Default: el N del generado que
#                    ya exista; si no hay, 0.
#      --check       no escribe: verifica (exit 4 si algo no cuadra)
#      --refresh     regenera y reporta drift (modo normal, idempotente)
#      --print-plan  muestra que haria, sin escribir
#      --force       permite --instance 0 en un worktree
#
#  exit: 0 OK · 2 uso · 3 entorno · 4 --check con problemas
# =============================================================================
set -u

OFFSET_BASE=10000
DB_PORT_BASE=5435
SENT_INI='>>> ring keys'
SENT_FIN='<<< ring keys'

prog=worktree_bootstrap
die()  { code=$1; shift; printf '%s: %s\n' "$prog" "$*" >&2; exit "$code"; }
say()  { printf '%s\n' "$*"; }
usage() {
    sed -n '4,26p' "$0" | sed -e 's/^# \{0,1\}//' >&2
    exit 2
}

# --- argumentos --------------------------------------------------------------
WT=""; N=""; mode=apply; force=0
while [ $# -gt 0 ]; do
    case "$1" in
        --instance)   [ $# -ge 2 ] || die 2 "--instance necesita un valor"; N="$2"; shift 2 ;;
        --instance=*) N="${1#--instance=}"; shift ;;
        --check)      mode=check;   shift ;;
        --refresh)    mode=apply;   shift ;;
        --print-plan) mode=plan;    shift ;;
        --force)      force=1;      shift ;;
        -h|--help)    usage ;;
        -*)           die 2 "opcion desconocida: $1" ;;
        *)            [ -z "$WT" ] || die 2 "sobra un argumento: $1"; WT="$1"; shift ;;
    esac
done
[ -n "$WT" ] || WT=.
[ -d "$WT" ] || die 3 "no existe la carpeta <$WT>"
WT="$(CDPATH= cd -- "$WT" && pwd)" || die 3 "no puedo resolver <$WT>"

PARAMS="$WT/run_working_v8/params"
BASE="$PARAMS/earthworm.d.base"
GEN="$PARAMS/earthworm.d"
ENVF="$PARAMS/db.env"
[ -d "$PARAMS" ] || die 3 "no existe $PARAMS (¿es un worktree de este repo? ¿ya tiene la Fase A?)"
[ -f "$BASE" ]  || die 3 "falta $BASE (tu rama es anterior al earthworm.d.base: actualizala)"

# --- repo principal ---------------------------------------------------------
common="$(git -C "$WT" rev-parse --git-common-dir 2>/dev/null)" || die 3 "<$WT> no es un repo git"
case "$common" in /*) ;; *) common="$WT/$common" ;; esac
MAIN="$(CDPATH= cd -- "$common/.." 2>/dev/null && pwd)" || die 3 "no puedo deducir el repo principal"

# --- N (instancia) ----------------------------------------------------------
# gen_N: deduce N del generado (offset de las keys / OFFSET_BASE). Vacio si no se puede.
gen_N() {
    [ -f "$GEN" ] || return 1
    python3 - "$BASE" "$GEN" "$OFFSET_BASE" <<'PY' || return 1
import re, sys
base, gen, ob = sys.argv[1], sys.argv[2], int(sys.argv[3])
def keys(p):
    out = []
    with open(p, newline='') as f:
        for l in f:
            m = re.match(r'^\s*Ring\s+(\S+)\s+(\d+)', l)
            if m:
                out.append((m.group(1), int(m.group(2))))
    return out
try:
    kb, kg = keys(base), keys(gen)
except OSError:
    sys.exit(1)
if not kb or len(kb) != len(kg):
    sys.exit(1)
off = {g[1] - b[1] for b, g in zip(kb, kg)}
if len(off) != 1:
    sys.exit(1)
o = off.pop()
if o < 0 or o % ob != 0:
    sys.exit(1)
print(o // ob)
PY
}

if [ -z "$N" ]; then
    if N="$(gen_N)"; then :; else N=0; fi
fi
case "$N" in ''|*[!0-9]*) die 2 "instancia invalida: '$N' (entero >= 0)" ;; esac
if [ "$N" -eq 0 ] && [ "$WT" != "$MAIN" ] && [ "$force" -eq 0 ]; then
    die 3 "instancia 0 en un worktree colisiona con las keys del repo principal ($MAIN): usa --instance N>=1 (o --force)"
fi
PORT=$((DB_PORT_BASE + N))

# --- generacion --------------------------------------------------------------
# gen <base> <salida> <offset_keys>
gen_keys() {
    python3 - "$1" "$2" "$3" "$SENT_INI" "$SENT_FIN" <<'PY'
import re, sys
base, out, off, tok_ini, tok_fin = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]

with open(base, newline='') as f:
    lines = f.readlines()

ini = fin = None
for i, l in enumerate(lines):
    if tok_ini in l and ini is None:
        ini = i
    elif tok_fin in l:
        fin = i
if ini is None or fin is None or fin <= ini:
    sys.stderr.write("bootstrap: no encuentro el par de centinelas de las keys en %s\n" % base)
    sys.exit(3)

ring_re = re.compile(r'^(\s*Ring\s+)(\S+)(\s+)(\d+)(\s*)(#.*)?(\r?\n?)$')
ring_any = re.compile(r'^\s*Ring\s+\S+\s+\d+')
names, keys = [], []
for i in range(ini + 1, fin):
    m = ring_re.match(lines[i])
    if not m:
        continue
    k = int(m.group(4)) + off
    names.append(m.group(2))
    keys.append(k)
    lines[i] = "%s%s%s%d%s%s%s" % (m.group(1), m.group(2), m.group(3), k,
                                   m.group(5), m.group(6) or "", m.group(7) or "")

# la transformacion tiene que cubrir TODAS las lineas Ring del fichero
total = sum(1 for l in lines if ring_any.match(l))
if not names or len(names) != total:
    sys.stderr.write("bootstrap: %d keys transformadas de %d lineas Ring en %s: "
                     "centinelas mal puestos\n" % (len(names), total, base))
    sys.exit(3)

with open(out, 'w', newline='') as f:
    f.writelines(lines)
print("%d anillos offset=%+d min=%d max=%d" % (len(names), off, min(keys), max(keys)))
PY
}

# --- db.env ------------------------------------------------------------------
db_port() { [ -f "$ENVF" ] && sed -n 's/^DB_PORT=\([0-9]\{1,\}\).*$/\1/p' "$ENVF" | head -1; }

# --- --check -----------------------------------------------------------------
if [ "$mode" = check ]; then
    rc=0
    ck() { printf 'CHECK  %s\n' "$*"; }
    bad() { printf 'FALLA  %s\n' "$*" >&2; rc=4; }

    if [ ! -f "$GEN" ]; then
        bad "no existe $GEN (ejecuta el bootstrap sin --check)"
    else
        tmp="$(mktemp)" || die 3 "mktemp fallo"
        if gen_keys "$BASE" "$tmp" "$((OFFSET_BASE * N))" >/dev/null 2>&1; then
            if cmp -s "$tmp" "$GEN"; then
                ck "earthworm.d coincide con base + offset $((OFFSET_BASE * N)) (N=$N)"
            else
                bad "drift: $GEN no coincide con base+offset (¿editado a mano? ejecuta --refresh)"
            fi
        else
            bad "no puedo regenerar (centinelas mal puestos en $BASE)"
        fi
        rm -f "$tmp"
        g="$(gen_N || true)"
        if [ "$g" = "$N" ]; then ck "instancia deducida del generado: N=$g"; else bad "el generado da N='$g' y se espera N=$N"; fi
    fi

    p="$(db_port)"
    if [ -z "$p" ]; then
        bad "falta $ENVF o su DB_PORT (necesita DB_PORT=$PORT)"
    elif [ "$p" = "$PORT" ]; then
        ck "db.env: DB_PORT=$p (= $DB_PORT_BASE + $N)"
    else
        bad "db.env: DB_PORT=$p y deberia ser $PORT (si no, los modulos apuntan a otra base)"
    fi

    # keys disjuntas con los demas worktrees del repo
    ours="$(python3 - "$GEN" <<'PY'
import re, sys
try:
    ks = [int(m.group(1)) for m in (re.match(r'^\s*Ring\s+\S+\s+(\d+)', l) for l in open(sys.argv[1])) if m]
except OSError:
    ks = []
print(' '.join(str(k) for k in ks))
PY
)"
    wt_list="$(git -C "$WT" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p')"
    others=0
    for w in $wt_list; do
        [ "$w" = "$WT" ] && continue
        g="$w/run_working_v8/params/earthworm.d"
        [ -f "$g" ] || continue
        others=$((others + 1))
        if [ ! -f "$w/run_working_v8/params/earthworm.d.base" ]; then
            bad "$w NO esta bootstrapeado (le falta earthworm.d.base): su earthworm.d es el versionado (offset 0) y colisiona con el nuestro; ejecuta alli: worktree_bootstrap.sh . --instance N (N>=1)"
            continue
        fi
        ov="$(python3 - "$g" "$ours" <<'PY'
import re, sys
try:
    ks = [int(m.group(1)) for m in (re.match(r'^\s*Ring\s+\S+\s+(\d+)', l) for l in open(sys.argv[1])) if m]
except OSError:
    ks = []
mine = set(int(x) for x in sys.argv[2].split())
print(' '.join(str(k) for k in ks if k in mine))
PY
)"
        if [ -n "$ov" ]; then bad "keys en comun con $g: $ov"; fi
    done
    ck "keys disjuntas con los otros worktrees (revisados: $others)"

    # puerto: si lo ocupa un contenedor, que sea el nuestro
    if command -v docker >/dev/null 2>&1; then
        owner="$(docker ps --filter "publish=$PORT" --format '{{.Names}}' 2>/dev/null | head -1)"
        want="ew8_hypodb"; [ "$N" -gt 0 ] && want="ew8_hypodb_$N"
        if [ -z "$owner" ]; then
            ck "puerto $PORT libre (la base de esta instancia no esta arriba)"
        elif [ "$owner" = "$want" ]; then
            ck "puerto $PORT ocupado por $owner (es el nuestro)"
        else
            bad "el puerto $PORT lo ocupa el contenedor <$owner>, no <$want>"
        fi
    fi

    # procesos vivos con OTRO EW_PARAMS (dos stacks a la vez)
    seen=""
    for e in /proc/[0-9]*/environ; do
        [ -r "$e" ] || continue
        v="$(tr '\0' '\n' < "$e" 2>/dev/null | sed -n 's/^EW_PARAMS=//p' | head -1)"
        [ -n "$v" ] || continue
        [ "$v" = "$PARAMS" ] && continue
        case " $seen " in *" $v "*) continue ;; esac
        seen="$seen $v"
        ck "stack vivo en OTRA carpeta: $v"
    done

    [ "$rc" -eq 0 ] && say "worktree_bootstrap --check: OK (instancia N=$N, puerto $PORT)"
    exit "$rc"
fi

# --- aplicar -----------------------------------------------------------------
say "worktree : $WT"
say "principal: $MAIN"
say "instancia: N=$N   (offset de keys +$((OFFSET_BASE * N)), puerto DB $PORT)"

if [ "$mode" = plan ]; then
    say "[plan] generaria $GEN desde $BASE"
    say "[plan] $( [ -f "$ENVF" ] && echo "verificaria $ENVF (DB_PORT=$PORT)" || echo "crearia $ENVF desde $MAIN (DB_PORT=$PORT)" )"
    exit 0
fi

drift=0
if [ -f "$GEN" ]; then
    tmpd="$(mktemp)"
    if gen_keys "$BASE" "$tmpd" "$((OFFSET_BASE * N))" >/dev/null 2>&1; then
        cmp -s "$tmpd" "$GEN" || drift=1
    fi
    rm -f "$tmpd"
fi
out="$(gen_keys "$BASE" "$GEN" "$((OFFSET_BASE * N))")" \
    || die 3 "no pude generar $GEN"
say "generado : $GEN ($out)"
[ "$drift" -eq 1 ] && say "           (habia drift: el anterior no coincidia con base+offset)"

if [ -f "$ENVF" ]; then
    p="$(db_port)"
    if [ -z "$p" ]; then
        die 3 "$ENVF no declara DB_PORT; ponle DB_PORT=$PORT a mano"
    elif [ "$p" != "$PORT" ]; then
        die 3 "$ENVF declara DB_PORT=$p y deberia ser $PORT (si no, los modulos apuntan a otra base)"
    fi
    say "db.env    : $ENVF ya correcto (DB_PORT=$p)"
else
    src="$MAIN/run_working_v8/params/db.env"
    [ -f "$src" ] || src="$MAIN/run_working_v8/params/db.env.example"
    [ -f "$src" ] || die 3 "no encuentro db.env ni db.env.example en $MAIN/run_working_v8/params"
    cp "$src" "$ENVF"
    if grep -q '^DB_PORT=' "$ENVF"; then
        sed -i "s|^DB_PORT=.*|DB_PORT=$PORT|" "$ENVF"
    else
        printf 'DB_PORT=%s\n' "$PORT" >> "$ENVF"
    fi
    say "db.env    : $ENVF creado desde $(basename "$src") con DB_PORT=$PORT"
fi

say ""
say "siguiente:"
say "  source ./ew8_unix.sh"
say "  make -C ew_gui_tools/common   &&   make -C ew_gui_tools/<modulo>"
say "  ./ew8_db.sh -i $N up            # solo si esta instancia usa su propia base"
say "  ./ew_monitor.sh start"
say "  ew_gui_tools/common/tools/worktree_bootstrap.sh . --check"
exit 0
