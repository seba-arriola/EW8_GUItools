#!/bin/sh
# Test offline de los conversores: tank -> store -> tank debe ser byte-identico.
# Es el proxy de AC1/AC7 (el .tank del store lo leen pick_FP/pickS sin cambios).
set -e

DIR=$(mktemp -d /tmp/wstore_conv_XXXXXX)
ROOT="$DIR/store"

./gen_tank "$DIR/x.tank" 5
./tank2store "$DIR/x.tank" --root "$ROOT"

STANK=$(find "$ROOT" -name '*.tank' | head -1)
if [ -z "$STANK" ]; then echo "FAIL: tank2store no genero segmento"; exit 1; fi
if ! cmp "$DIR/x.tank" "$STANK"; then echo "FAIL: .tank del store != original"; exit 1; fi

./store2tank --root "$ROOT" --scnl CX.ABC.--.HHZ \
    --start 1700000000 --end 1700000010 --out "$DIR/out.tank"
if ! cmp "$DIR/x.tank" "$DIR/out.tank"; then echo "FAIL: store2tank != original"; exit 1; fi

./wstore_query --root "$ROOT" > /dev/null
rm -r "$DIR"
echo "test_convert OK"
