#!/bin/sh
# test_offline.sh — test integrado del modo offline de pickS.
# Determinista: dos corridas sobre el mismo tank deben ser idénticas y debe
# emitirse al menos un pick S.

set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
DIR=${TMPDIR:-/tmp}/opencode/pickS_offline$$
mkdir -p "$DIR"
trap 'rm -rf "$DIR"' EXIT

cat > "$DIR/pickS.d" <<EOF
MyModuleId MOD_PICKS
StaFile $DIR/pickS.sta
GuideMode independent
Debug 3
FilterLowHz 1.0
FilterHighHz 10.0
FilterOrder 4
StaLenSec 0.5
LtaLenSec 5.0
TriggerOn 3.0
TriggerOff 1.5
AicWinSec 1.0
PolWinSec 1.0
DeadTimeSec 2.0
BufferSec 30.0
MinSnr 2.0
MaxWeight 4
ReportChan detected
EOF

cat > "$DIR/pickS.sta" <<EOF
1 1 TEST C -- HHE HHN HHZ
EOF

"$HERE/test/gen_tank" "$DIR/t.tank" TEST C -- HHE HHN HHZ

"$HERE/pickS" "$DIR/pickS.d" "$DIR/t.tank" > "$DIR/out1.txt" 2> "$DIR/err1.txt"
"$HERE/pickS" "$DIR/pickS.d" "$DIR/t.tank" > "$DIR/out2.txt" 2> "$DIR/err2.txt"

if ! diff -q "$DIR/out1.txt" "$DIR/out2.txt" > /dev/null; then
    echo "FAIL: salidas no deterministas"
    diff "$DIR/out1.txt" "$DIR/out2.txt"
    exit 1
fi

if ! grep -q ' S$' "$DIR/out1.txt"; then
    echo "FAIL: no se emitio ningun pick S"
    cat "$DIR/err1.txt"
    exit 1
fi

echo "picks S emitidos:"
cat "$DIR/out1.txt"
echo "test_offline OK"
