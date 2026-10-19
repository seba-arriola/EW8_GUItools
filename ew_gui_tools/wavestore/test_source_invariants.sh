#!/bin/sh
# Invariantes de FUENTE de wavestore (grep; sin GTK, sin anillos, sin store real).
#
# Protegen dos cosas que no se pueden probar desde un test unitario:
#   1) que un fallo del ingester no vuelva a ser MUDO: log propio en EW_LOG, con
#      el prefijo del modulo (es lo que busca `diag.sh log wstore_ingest`);
#   2) que `logit` quede en el hilo principal del anillo: la capa de escritura
#      (wstore_writer.c) y la libreria headless (config/filtro) no dependen de
#      EarthWorm, y asi los tests las enlazan sin libew_mt.
set -u
cd "$(dirname "$0")" || exit 1
fails=0

ck() {  # ck  <descripcion> <patron ERE> <archivo>
    if grep -qE "$2" "$3"; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails+1)); fi
}
nck() { # nck <descripcion> <patron ERE> <archivo>   (no debe aparecer)
    if grep -qE "$2" "$3"; then echo "FAIL: $1"; fails=$((fails+1)); else echo "ok  : $1"; fi
}

# --- Log propio (ver AGENTS.md §6) -----------------------------------------
ck  "log: el modulo tiene log propio (logit_init)"        'logit_init\('             wstore_ingest_main.c
ck  "log: el prefijo es el modulo (no argv[1]=wavestore.d)" 'logit_init\("wstore_ingest"' wstore_ingest_main.c
ck  "log: honra el LogFile del .d"                        'cfg\.log_file\)'          wstore_ingest_main.c
ck  "log: el modid sale de MyModuleId (GetModId)"         'GetModId\(cfg\.module_id' wstore_ingest_main.c

# --- La purga pasa siempre por el helper que deja conteo en el log ---------
ck  "purga: el anillo usa purge_and_log"                  'purge_and_log\('          wstore_ingest.c
nck "purga: nadie llama al prune sin conteo"              'wstore_prune\('           wstore_ingest.c

# --- logit solo desde el hilo principal ------------------------------------
nck "hilos: el escritor del shard no loguea"              'logit\('                  wstore_writer.c
nck "libreria: wstore_config.c no depende de Earthworm"   'logit\('                  wstore_config.c
nck "libreria: wstore_filter.c no depende de Earthworm"   'logit\('                  wstore_filter.c
for fn in shard_fn shard_writer; do
    if sed -n "/^static .*${fn}(/,/^}/p" wstore_ingest.c | grep -q 'logit('; then
        echo "FAIL: hilos: $fn no debe llamar a logit"; fails=$((fails+1))
    else
        echo "ok  : hilos: $fn no llama a logit"
    fi
done

if [ "$fails" -eq 0 ]; then
    echo "OK: invariantes de wavestore"
fi
exit $fails
