#!/bin/sh
# Invariantes de FUENTE de hypodb_ingest (grep; sin GTK, sin BD, sin anillos).
#
# Protegen los arreglos del SIGABRT del 2026-10-14: el modulo arrancaba bien,
# escribia en la BD y moria 9-77 minutos despues, SIN log, por escribir fuera del
# array de canales (`rd_chan_mag` de EarthWorm no acota sus escrituras) y por
# tratar como cadena C un buffer de anillo que no viene terminado en NUL.
set -u
cd "$(dirname "$0")" || exit 1
fails=0

ck() {  # ck  <descripcion> <patron ERE> <archivo>
    if grep -qE "$2" "$3"; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails+1)); fi
}
nck() { # nck <descripcion> <patron ERE> <archivo>   (no debe aparecer)
    if grep -qE "$2" "$3"; then echo "FAIL: $1"; fails=$((fails+1)); else echo "ok  : $1"; fi
}

# --- Dimensionado del array de canales: rd_chan_mag NO acota contra el tamano ---
ck  "canales: usa el helper compartido con csnhypodbp"  'nchan = csnhypodbp_magchan_parse' hypodb_ingest.c
nck "canales: ya no cuenta '\\n' como proxy"           'nchan = lines - 1'                hypodb_ingest.c
nck "canales: ya no recorre hasta NUL (sin cota)"      'for \(p = msg; \*p; p\+\+\)'      hypodb_ingest.c
nck "canales: sin parseo local de canales"             'static int parse_channels'        hypodb_ingest.c
nck "canales: sin contador local de canales"           'hypodb_mag_station_count'         hypodb_ingest.c
ck  "canales: el helper compartido parsea sobre copia" 'calloc\(len \+ 64, 1\)'           ../csnhypodbp/csnhypodbp_magchan.c

# --- Agrupacion: el matcher indexa claves, no structs de grupo ---
nck "grupos: ya no castea HypodbGroup[] a claves"      '\(const CsnhypodbpGroupKey \*\)st->groups' hypodb_ingest.c
ck  "grupos: pasa claves reales al matcher"            'csnhypodbp_group_matches\(keys,'  hypodb_ingest.c

# --- Contrato de entrada: el buffer del anillo no viene terminado en NUL ---
ck  "anillo: el buffer se termina en NUL"              'buf\[size\] = .\\0.'              hypodb_ingest_main.c
ck  "anillo: se reserva 1 byte para el NUL"            'bufsz = 65536 \+ 1'               hypodb_ingest_main.c
ck  "anillo: se pasa bufsz - 1 a tport_getmsg"         'buf, bufsz - 1'                   hypodb_ingest_main.c

# --- Parada: TERMINATE y el pid, tambien dentro del drenado ---
ck  "parada: mira TERMINATE y el pid del modulo"       'terminate_requested\('                          hypodb_ingest_main.c
ck  "parada: se comprueba dentro del drenado"          'terminate_requested\(&r\[0\], mypid\) &&'        hypodb_ingest_main.c
nck "parada: ya no mira solo TERMINATE"                'tport_getflag\(&r\[0\]\) != TERMINATE\)'        hypodb_ingest_main.c

# --- Que un fallo no vuelva a ser mudo ---
ck  "log: el modulo tiene log propio (logit_init)"     'logit_init\('                     hypodb_ingest_main.c
ck  "log: honra el LogFile del .d"                     'cfg\.log_file\)'                  hypodb_ingest_main.c

# --- Confirmaciones manuales: fila `origin` propia, sin parsear el id a mano ---
ck  "confirmacion: se ingiere por su propia ruta"      'hypodb_feed_confirmed\('          hypodb_ingest_main.c
ck  "confirmacion: id con la version del ARC"          'confirmed \? sol->version : 0'    hypodb_ingest.c
ck  "confirmacion: sin guarda de version"              'NO pasa por la guarda de version' hypodb_ingest.c
ck  "confirmacion: no toca la meta del grupo"          'if \(!confirmed\)'                hypodb_ingest.c
ck  "preferida: se calcula desde la BD"                'ewguidb_query_origins\(st->db, g->event_id' hypodb_ingest.c
ck  "preferida: no borra la magnitud si no hay candidata" 'ewguidb_set_preferred_origin'  hypodb_ingest.c
ck  "picks: se drenan ANTES que los ARC"               'PICK_RING PRIMERO'                hypodb_ingest_main.c
nck "ids: sin strrchr sobre origin_id"                 'strrchr\(ors\[j\]\.origin_id'     hypodb_ingest.c
nck "ids: sin derivar el mod a mano"                   'colon \? atoi\(colon \+ 1\)'      hypodb_ingest.c

# --- Historial de iteraciones de un refinador vivo (live_usage=1) ---
# Solo los localizadores con live_usage=1 escriben en HYPO_RING_REF, asi que el
# flag de historial sale del anillo (i == 1), no de una lista de mods duplicada.
ck  "iteraciones: el flag sale del anillo REF"       'int history = \(i == 1\)'       hypodb_ingest_main.c
ck  "iteraciones: el flag llega a hypodb_feed_arc"   'hypodb_feed_arc\(st, buf, \(size_t\)size, mod, history\)' hypodb_ingest_main.c
ck  "iteraciones: id con el marcador :I"             'ewgui_origin_id_build_iter'     hypodb_ingest.c
ck  "iteraciones: sin guarda de version"             'if \(!history\)'                hypodb_ingest.c
ck  "iteraciones: version 1..9999 obligatoria"       'sol->version == 0 \|\| sol->version > 9999u' hypodb_ingest.c
ck  "iteraciones: la meta se actualiza (ancla mags)" 'update_origin_meta\(g, origin_id, mod, sol->manual, sol->version' hypodb_ingest.c
ck  "magnitudes: se proyectan a todas las filas del mod" 'omod != mod \|\| oconf'     hypodb_ingest.c

if [ "$fails" -eq 0 ]; then
    echo "OK: invariantes de hypodb_ingest"
fi
exit $fails
