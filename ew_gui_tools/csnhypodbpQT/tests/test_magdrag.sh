#!/bin/sh
# Arrastre de la franja de ventana de magnitudes.
#
# El dueno del gesto es (estacion, magnitud): ajustar A, cambiar a B y
# arrastrar B debe cambiar SOLO B, y un press sin release en B no puede dejar
# el encuadre congelado ni bloquear la franja de la siguiente estacion (el
# fallo reportado: "cambie de estacion y ya no se podia ajustar la ventana").
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../csnhypodbpQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/csnhypodbpQT
if [ ! -x "$bin" ]; then
    echo "SKIP: falta $bin"
    exit 0
fi
if [ ! -f "$params/csnhypodbpqt.d" ]; then
    echo "SKIP: falta $params/csnhypodbpqt.d"
    exit 0
fi

state=$(mktemp -d "${TMPDIR:-/tmp}/bpq_magdrag.XXXXXX") || exit 2
trap 'rm -rf "$state"' EXIT INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
    BPQ_MODE=--magdrag BPQ_TEST_FIXTURE=pins BPQ_TAB=magnitudes \
    BPQ_MAGDRAG_STA1=PB01 BPQ_MAGDRAG_STA2=PB02 \
    BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
    "$bin" csnhypodbpqt.d 2>/dev/null )
rc=$?

if [ "$rc" = 0 ]; then
    ok "magdrag: el modo headless corre"
else
    bad "magdrag: el modo headless salio con $rc"
    printf '%s\n' "$out" | sed 's/^/       /'
fi

pair() {  # <tag> <before|after>
    printf '%s\n' "$out" | sed -n \
        "s/^magdrag $1 .* $2=\([-0-9.]*,[-0-9.]*\) .*/\1/p"
}

A_b=$(pair A before)
A_a=$(pair A after)
B_b=$(pair B before)
B_a=$(pair B after)
Ab_b=$(pair abort before)
Ab_a=$(pair abort after)
A2_b=$(pair A2 before)
A2_a=$(pair A2 after)

snapA() {  # <tag>
    printf '%s\n' "$out" | sed -n \
        "s/^magdrag snap $1 A=\([-0-9.]*,[-0-9.]*\) B=.*/\1/p"
}
snapB() {  # <tag>
    printf '%s\n' "$out" | sed -n \
        "s/^magdrag snap $1 A=[-0-9.]*,[-0-9.]* B=\([-0-9.]*,[-0-9.]*\)$/\1/p"
}

sA_A=$(snapA A)
sB_A=$(snapB A)
sA_B=$(snapA B)
sB_B=$(snapB B)
sA_ab=$(snapA abort)
sB_ab=$(snapB abort)
sA_A2=$(snapA A2)
sB_A2=$(snapB A2)

# 1) El primer drag cambia la ventana de A; B sigue con la del .d.
if [ -n "$A_b" ] && [ "$A_a" != "$A_b" ] && [ "$sA_A" = "$A_a" ]; then
    ok "A: el drag cambia su ventana ($A_b -> $A_a)"
else
    bad "A: el drag no cambio la ventana <$A_b -> $A_a, snap=$sA_A>"
fi

# 2) Cambiar a B y arrastrar cambia B; la ventana de A no se toca.
if [ -n "$B_b" ] && [ "$B_a" != "$B_b" ] && [ "$sB_B" = "$B_a" ]; then
    ok "B: tras cambiar de estacion el drag funciona ($B_b -> $B_a)"
else
    bad "B: no se pudo arrastrar tras cambiar de estacion <$B_b -> $B_a>"
fi
if [ "$sA_B" = "$A_a" ]; then
    ok "A: el drag de B no toco la ventana de A"
else
    bad "A: el drag de B piso la ventana de A <$A_a -> $sA_B>"
fi

# 3) Press sin release (gesto abortado) en B: ninguna ventana cambia.
if [ "$sA_ab" = "$sA_B" ] && [ "$sB_ab" = "$sB_B" ] &&
   [ "$Ab_a" = "$Ab_b" ]; then
    ok "abort: el press sin release no modifica ninguna ventana"
else
    bad "abort: el press sin release escribio una ventana <A $sA_B->$sA_ab B $sB_B->$sB_ab>"
fi
ck_abort=$(printf '%s\n' "$out" | grep -c '^magdrag abort .*abort=1$')
if [ "$ck_abort" = 1 ]; then
    ok "abort: el gesto se registro como press sin release"
else
    bad "abort: falta la marca abort=1"
fi

# 4) Volver a A y arrastrar: el aborto no dejo el estado pegado.
if [ "$A2_b" = "$sA_ab" ] && [ "$A2_a" != "$A2_b" ] &&
   [ "$sA_A2" = "$A2_a" ]; then
    ok "A2: tras el gesto abortado la franja sigue respondiendo ($A2_b -> $A2_a)"
else
    bad "A2: la franja quedo bloqueada tras el aborto <$A2_b -> $A2_a>"
fi
if [ "$sB_A2" = "$sB_ab" ]; then
    ok "B: el arrastre posterior de A no toco la ventana de B"
else
    bad "B: el arrastre de A piso la ventana de B <$sB_ab -> $sB_A2>"
fi

ntouch=$(printf '%s\n' "$out" | sed -n 's/^magdrag A2 .* touched=\([0-9]*\) .*/\1/p')
if [ "$ntouch" = 2 ]; then
    ok "touched: dos (estacion, magnitud) ajustadas a mano"
else
    bad "touched: se esperaban 2, hay <$ntouch>"
fi

echo "== magdrag checks=$((pass + fail)) fallas=$fail =="
[ "$fail" = 0 ]
