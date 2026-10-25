#!/bin/sh
# test_pin_invariants.sh - Invariantes de fuente de la fase 7 (origen fijado).
#
# No abre ventana: solo comprueba que el cableado que hace posible el pin no
# desaparezca en un refactor.
#
# Uso: sh tests/test_pin_invariants.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
src=$here/../ui/bpq_mainwindow.cxx
hdr=$here/../ui/bpq_mainwindow.h
model=$here/../ui/bpq_events_model.cxx
pin_h=$here/../c/bpq_pin.h

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for f in "$src" "$hdr" "$model" "$pin_h"; do
    if [ -f "$f" ]; then ok "existe $(basename "$f")"; else bad "falta $f"; fi
done

# El doble clic de Origins debe estar conectado al camino que fija.
if grep -q 'QTableView::doubleClicked' "$src" &&
   grep -q 'onOriginDoubleClicked' "$src"; then
    ok "Origins: doubleClicked -> onOriginDoubleClicked"
else
    bad "Origins no conecta doubleClicked con el pin"
fi

# El pin debe mandar en la solucion actual y en la tabla de estaciones.
if grep -q 'pinnedSolutionIndex()' "$src" &&
   grep -q 'currentSolution()' "$src"; then
    ok "Procesar consulta pinnedSolutionIndex/currentSolution"
else
    bad "Procesar no consulta el pin"
fi
if grep -q 'int pi = pinnedSolutionIndex();' "$src"; then
    ok "refreshStations/renderPreview usan el pin como pi"
else
    bad "refreshStations/renderPreview no usan el pin"
fi

# El candidato se identifica por clave, nunca por indice fragil.
if grep -q 'm_candKey' "$src" && ! grep -q 'm_candForSel' "$src" "$hdr"; then
    ok "candidata por clave (sin m_candForSel)"
else
    bad "queda m_candForSel o falta m_candKey"
fi

# La clave es la de la solucion de ORIGEN: el ARC offline devuelve mod=0 y el
# refinador puede devolver qid/version=0. Recalcularla desde m_cand la oculta.
if grep -q 'm_candKey = solKey(sol)' "$src" &&
   ! grep -q 'm_candKey = solKey(&m_cand)' "$src"; then
    ok "candidata: clave de la solucion de origen"
else
    bad "la clave de la candidata se recalcula desde m_cand"
fi

# Una sola definicion del formato de identidad.
if grep -q 'bpq_origin_key' "$model" "$src"; then
    ok "la clave de origen sale de bpq_origin_key"
else
    bad "no se usa bpq_origin_key"
fi
if grep -q 'bpq_pin_reconcile\|bpq_pin_clear' "$src"; then
    ok "el pin se limpia/reconcilia (evento o solucion ausente)"
else
    bad "no hay limpieza del pin"
fi

# El dump debe exponer el estado del pin (tests end-to-end).
if grep -q 'pin key=' "$src"; then
    ok "dumpTree expone pin key="
else
    bad "dumpTree no expone el pin"
fi

# commonqt: ninguna tabla reimplementa el orden.
if grep -q 'lessThan' "$src" "$model"; then
    bad "hay un lessThan propio (debe usarse commonqt::SortProxy)"
else
    ok "sin lessThan propio (orden en commonqt)"
fi

echo "----"
echo "== pin invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
