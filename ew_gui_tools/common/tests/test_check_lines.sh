#!/bin/sh
# Test del validador de capas de lineas (make_overlays.py check-lines).
# Fixtures sinteticos: estructura (rc != 0) y coherencia (solo aviso).
set -u
cd "$(dirname "$0")" || exit 1
PY=../tools/make_overlays.py
TMP="${TMPDIR:-/tmp}/check_lines_$$"
mkdir -p "$TMP" || exit 1
fails=0
ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fails=$((fails + 1)); }

# --- fixture coherente: 2 capas, sin saltos ---
cat > "$TMP/ok.lines" <<'EOF'
#layer name=a color=FF0000 width=2
-70 -30
-70.1 -30.1
>
-71 -31
-71.1 -31.1
-71.2 -31.2
#layer name=b
-72 -32
-72.5 -32.5
EOF
python3 "$PY" check-lines "$TMP/ok.lines" > "$TMP/ok.out" 2>&1
rc=$?
[ "$rc" -eq 0 ] && ok "coherente -> rc 0" || bad "coherente -> rc $rc"
grep -q "capa 'a'" "$TMP/ok.out" && ok "reporta la capa a" || bad "no reporta la capa a"
grep -q "polilineas=2" "$TMP/ok.out" && ok "cuenta 2 polilineas" || bad "no cuenta 2 polilineas"
grep -q "capa 'b'" "$TMP/ok.out" && ok "reporta la capa b" || bad "no reporta la capa b"

# --- vertice mal formado -> rc != 0 ---
printf '#layer name=a\n-70 -30\nsolo_una\n' > "$TMP/bad.lines"
python3 "$PY" check-lines "$TMP/bad.lines" > "$TMP/bad.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "vertice mal formado -> rc != 0" || bad "mal formado -> rc $rc"
grep -q "mal formado" "$TMP/bad.out" && ok "avisa del vertice mal formado" || bad "no avisa del vertice"

# --- coordenada fuera de rango -> rc != 0 ---
printf '#layer name=a\n-200 -30\n-70 -30\n' > "$TMP/range.lines"
python3 "$PY" check-lines "$TMP/range.lines" > "$TMP/range.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "fuera de rango -> rc != 0" || bad "fuera de rango -> rc $rc"
grep -q "fuera de rango" "$TMP/range.out" && ok "avisa de la coordenada" || bad "no avisa del rango"

# --- coordenada no numerica -> rc != 0 ---
printf '#layer name=a\nx y\n-70 -30\n' > "$TMP/nan.lines"
python3 "$PY" check-lines "$TMP/nan.lines" > "$TMP/nan.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "no numerica -> rc != 0" || bad "no numerica -> rc $rc"

# --- capa sin polilineas -> rc != 0 ---
printf '#layer name=vacia\n' > "$TMP/empty.lines"
python3 "$PY" check-lines "$TMP/empty.lines" > "$TMP/empty.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "capa vacia -> rc != 0" || bad "capa vacia -> rc $rc"
grep -q "sin polilineas" "$TMP/empty.out" && ok "avisa de la capa vacia" || bad "no avisa de la capa vacia"

# --- archivo sin capas -> rc != 0 ---
printf -- '-70 -30\n-70.1 -30.1\n' > "$TMP/nolayer.lines"
python3 "$PY" check-lines "$TMP/nolayer.lines" > "$TMP/nolayer.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "sin #layer -> rc != 0" || bad "sin #layer -> rc $rc"

# --- archivo ausente -> rc != 0 ---
python3 "$PY" check-lines "$TMP/no_existe.lines" > "$TMP/nf.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "archivo ausente -> rc != 0" || bad "ausente -> rc $rc"

# --- salto grande: solo avisa (rc 0) y lo reporta ---
printf '#layer name=salto\n-70 -30\n-60 -30\n' > "$TMP/jump.lines"
python3 "$PY" check-lines "$TMP/jump.lines" --max-jump 2.0 > "$TMP/jump.out" 2>&1
rc=$?
[ "$rc" -eq 0 ] && ok "salto grande -> rc 0 (avisa)" || bad "salto -> rc $rc"
grep -q "salto" "$TMP/jump.out" && ok "reporta el salto" || bad "no reporta el salto"
python3 "$PY" check-lines "$TMP/jump.lines" --max-jump 20.0 > "$TMP/jump2.out" 2>&1
grep -q "salto de" "$TMP/jump2.out" && bad "con --max-jump 20 no deberia avisar" \
    || ok "el umbral de salto es configurable"

# --- polilinea de un solo vertice: la capa queda sin dibujables -> rc != 0 ---
printf '#layer name=uno\n-70 -30\n' > "$TMP/tiny.lines"
python3 "$PY" check-lines "$TMP/tiny.lines" > "$TMP/tiny.out" 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "solo 1 vertice -> rc != 0 (capa sin dibujables)" || bad "1 vertice -> rc $rc"
grep -q "1 vertice" "$TMP/tiny.out" && ok "avisa de la polilinea corta" || bad "no avisa de la corta"

# --- duplicado consecutivo: avisa ---
printf '#layer name=dup\n-70 -30\n-70 -30\n-70.1 -30.1\n' > "$TMP/dup.lines"
python3 "$PY" check-lines "$TMP/dup.lines" > "$TMP/dup.out" 2>&1
rc=$?
[ "$rc" -eq 0 ] && ok "duplicado -> rc 0" || bad "duplicado -> rc $rc"
grep -q "duplicado" "$TMP/dup.out" && ok "avisa del duplicado" || bad "no avisa del duplicado"

# --- sin argumentos -> rc 2 ---
python3 "$PY" check-lines > "$TMP/none.out" 2>&1
rc=$?
[ "$rc" -eq 2 ] && ok "sin archivos -> rc 2" || bad "sin archivos -> rc $rc"

rm -rf "$TMP"
if [ "$fails" -gt 0 ]; then echo "FALLOS: $fails"; exit 1; fi
echo "OK test_check_lines"
exit 0
