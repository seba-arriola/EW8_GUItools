#!/bin/sh
# test_offline.sh - test integrado determinista del modo offline de csnmags.
# No arranca anillos ni Earthworm: genera un tank y un hypos.jsonl sintéticos.
set -e
cd "$(dirname "$0")/.."

mkdir -p test/tmp test/responses
for s in TST1 TST2; do
    for c in HHZ HHN HHE; do
        printf 'CONSTANT 1.0\nZEROS 0\nPOLES 0\n' > "test/responses/${s}_${c}_C1.pz"
    done
done

./test/gen_hypos test/tmp/hypos.jsonl
./test/gen_tank  test/tmp/test.tank

./csnmags test/csnmags_test.d test/tmp/hypos.jsonl test/tmp/test.tank \
    --out test/tmp/out1.jsonl > test/tmp/log1.txt
./csnmags test/csnmags_test.d test/tmp/hypos.jsonl test/tmp/test.tank \
    --out test/tmp/out2.jsonl > test/tmp/log2.txt

if ! diff -q test/tmp/out1.jsonl test/tmp/out2.jsonl >/dev/null; then
    echo "test_offline: FALLO - no es determinista"
    diff test/tmp/out1.jsonl test/tmp/out2.jsonl || true
    exit 1
fi

if ! grep -qE '"ML":[0-9]' test/tmp/out1.jsonl; then
    echo "test_offline: FALLO - ML no calculada"
    cat test/tmp/out1.jsonl
    exit 1
fi

echo "test_offline OK"
