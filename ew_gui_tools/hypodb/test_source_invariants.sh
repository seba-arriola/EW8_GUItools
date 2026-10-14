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
ck  "canales: dimensiona con lo que escribe el motor"  'nchan = hypodb_mag_station_count' hypodb_ingest.c
nck "canales: ya no cuenta '\\n' como proxy"           'nchan = lines - 1'                hypodb_ingest.c
nck "canales: ya no recorre hasta NUL (sin cota)"      'for \(p = msg; \*p; p\+\+\)'      hypodb_ingest.c
ck  "canales: parsea sobre copia con holgura"          'calloc\(len \+ 64, 1\)'           hypodb_ingest.c

# --- Agrupacion: el matcher indexa claves, no structs de grupo ---
nck "grupos: ya no castea HypodbGroup[] a claves"      '\(const CsnhypodbpGroupKey \*\)st->groups' hypodb_ingest.c
ck  "grupos: pasa claves reales al matcher"            'csnhypodbp_group_matches\(keys,'  hypodb_ingest.c

# --- Contrato de entrada: el buffer del anillo no viene terminado en NUL ---
ck  "anillo: el buffer se termina en NUL"              'buf\[size\] = .\\0.'              hypodb_ingest_main.c
ck  "anillo: se reserva 1 byte para el NUL"            'bufsz = 65536 \+ 1'               hypodb_ingest_main.c
ck  "anillo: se pasa bufsz - 1 a tport_getmsg"         'buf, bufsz - 1'                   hypodb_ingest_main.c

# --- Que un fallo no vuelva a ser mudo ---
ck  "log: el modulo tiene log propio (logit_init)"     'logit_init\('                     hypodb_ingest_main.c
ck  "log: honra el LogFile del .d"                     'cfg\.log_file\)'                  hypodb_ingest_main.c

if [ "$fails" -eq 0 ]; then
    echo "OK: invariantes de hypodb_ingest"
fi
exit $fails
