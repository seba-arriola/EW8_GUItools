#!/bin/sh
# test_ttp.sh — prueba del binario ttp (predicción P/S IASP91).
# Valida: 1 fila por estación, S>P, valores positivos, determinismo.

set -e
# mawk respeta LC_NUMERIC: con locale no-C compara numeros como strings.
LC_ALL=C
export LC_ALL

HERE=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/sta.txt" <<EOF
FAR1 C HHZ -- -33.33770 -70.29940 2770.0 1
GO01 C HHZ -- -19.66850 -69.19420 3809.0 1
EOF

cat > "$TMP/ev.tsv" <<EOF
1 -30.0 -71.0 20.0
EOF

( cd "$HERE" && ./ttpred "$TMP/sta.txt" "$TMP/ev.tsv" ) > "$TMP/out1.txt"
( cd "$HERE" && ./ttpred "$TMP/sta.txt" "$TMP/ev.tsv" ) > "$TMP/out2.txt"

if ! diff -q "$TMP/out1.txt" "$TMP/out2.txt" >/dev/null; then
    echo "FAIL: salida no determinista"
    diff "$TMP/out1.txt" "$TMP/out2.txt"
    exit 1
fi

n=$(wc -l < "$TMP/out1.txt")
if [ "$n" -ne 2 ]; then
    echo "FAIL: se esperaban 2 filas, hay $n"
    cat "$TMP/out1.txt"
    exit 1
fi

if ! awk '{
    if (NF < 5) exit 1;
    if ($3 <= 0 || $3 >= 180) exit 1;
    if ($4 <= 0 || $5 <= 0) exit 1;
    if (!($5 > $4)) exit 1;
}' "$TMP/out1.txt"; then
    echo "FAIL: valores de TT no plausibles"
    cat "$TMP/out1.txt"
    exit 1
fi

echo "test_ttp OK"
cat "$TMP/out1.txt"
