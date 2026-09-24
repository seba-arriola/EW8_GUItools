#!/bin/sh
# Ejecuta la bateria de tests standalone de csnloc.
# Necesita iasp91.tbl/.hed en el cwd (se copian a run_working_v8/params).
cd "$(dirname "$0")" || exit 1
fail=0
for t in test/test_pick_parse test/test_repick test/test_dbscan \
         test/test_hypo_format test/test_tt test/test_synth_locate \
         test/test_two_events; do
    if [ ! -x "$t" ]; then echo "SKIP (no compilado): $t"; continue; fi
    echo "=== $t ==="
    ./$t || fail=1
done
exit $fail
