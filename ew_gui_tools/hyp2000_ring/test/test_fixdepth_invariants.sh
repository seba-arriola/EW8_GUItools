#!/bin/sh
# Invariantes de fuente de la profundidad fija en hyp2000_ring: la clave se lee
# del .d y el `ZTR <z> T` se emite DESPUES del @CommandFile (si se emitiera
# antes, el .hyp lo pisaria con su `ZTR 20.0 F`). Sin motor ni anillos.
set -u
cd "$(dirname "$0")/.." || exit 1
fails=0

ck() {  # ck <descripcion> <patron ERE>
    if grep -qE "$2" hyp2000_ring.c; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails+1)); fi
}
nck() { # nck <descripcion> <patron ERE>
    if grep -qE "$2" hyp2000_ring.c; then echo "FAIL: $1"; fails=$((fails+1)); else echo "ok  : $1"; fi
}

ck "config: lee FixedDepthKm"          'k_its\("FixedDepthKm"\)'
ck "config: default es -1 (libre)"      'FixedDepthKm = -1\.0'
ck "motor: emite ZTR <z> T"             'ZTR %\.3f T'
ck "motor: solo si FixedDepthKm >= 0"   'if \(FixedDepthKm >= 0\.0\)'
ck "motor: el 2.o token es T (DEPFIXA)" 'ZTR %\.3f T'
nck "no fija ZTR de forma incondicional" 'callHypo\("ZTR'

at_line=$(grep -n 'snprintf(msg, sizeof(msg), "@%s", CommandFile)' hyp2000_ring.c | head -1 | cut -d: -f1)
ztr_line=$(grep -n 'ZTR %\.3f T' hyp2000_ring.c | head -1 | cut -d: -f1)
if [ -n "$at_line" ] && [ -n "$ztr_line" ] && [ "$at_line" -lt "$ztr_line" ]; then
    echo "ok  : ZTR va despues del @CommandFile"
else
    echo "FAIL: ZTR debe ir despues del @CommandFile (at=$at_line ztr=$ztr_line)"
    fails=$((fails+1))
fi

if [ "$fails" -gt 0 ]; then echo "$fails FALLOS"; exit 1; fi
echo "OK test_fixdepth_invariants"
