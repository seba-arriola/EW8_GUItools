#!/bin/sh
# Test offline de los conversores: traces -> store -> traces debe ser byte-identico.
# Es el proxy de AC1/AC7 (el .traces del store lo leen pick_FP/pickS sin cambios).
set -e

DIR=$(mktemp -d /tmp/wstore_conv_XXXXXX)
ROOT="$DIR/store"

./gen_traces "$DIR/x.traces" 5
./wstore_import "$DIR/x.traces" --root "$ROOT"

SSEG=$(find "$ROOT" -name '*.ws' | head -1)
if [ -z "$SSEG" ]; then echo "FAIL: wstore_import no genero segmento"; exit 1; fi
if ! cmp "$DIR/x.traces" "$SSEG"; then echo "FAIL: segmento del store != original"; exit 1; fi

./wstore_export --root "$ROOT" --scnl CX.ABC.--.HHZ \
    --start 1700000000 --end 1700000010 --out "$DIR/out.traces"
if ! cmp "$DIR/x.traces" "$DIR/out.traces"; then echo "FAIL: wstore_export != original"; exit 1; fi

./wstore_query --root "$ROOT" > /dev/null
rm -r "$DIR"
echo "test_convert OK"
