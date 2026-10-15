#!/bin/bash
# Invariante de FUENTE: `AGENTS.md` debe seguir documentando como se lanza un
# modulo suelto (§6) con los datos que hacen falta para que funcione a la
# primera: el entorno cargado, la carpeta de los `.d`, la estructura general, los
# binarios cuyo nombre NO coincide con su carpeta y las trampas de entorno.
#
# Por que: sin esa receta, un agente pide "ejecuta X" y el modulo no arranca (o
# arranca sin su configuracion) porque falta el entorno o el directorio. Ver
# AGENTS §6 y §7.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DOC="$ROOT/AGENTS.md"
fail=0

need() {  # need <descripcion> <texto literal>
    if grep -qF -- "$2" "$DOC"; then
        echo "ok  : $1"
    else
        echo "FAIL: $1 (falta en AGENTS.md: $2)"
        fail=1
    fi
}

[ -f "$DOC" ] || { echo "FAIL: no existe $DOC"; exit 1; }

need "receta: terminal independiente"      'terminal independiente'
need "receta: cargar el entorno"           'source ./ew8_unix.sh'
need "receta: la carpeta de los .d"        'cd run_working_v8/params'
need "receta: estructura general"          '<binario> <binario>.d'
need "receta: el .d es el de EarthWorm"    'Se detiene con `Ctrl-C`'
need "binario real de hypodb"              'hypodb_ingest hypodb.d'
need "binario real de wavestore"           'wstore_ingest wavestore.d'
need "aviso: GUI necesita DISPLAY"         'no arranca sin `DISPLAY`'
need "aviso: productor duplicado"          'dos escribiendo en los anillos'
need "aviso: timeout de SeisComP"          '/usr/bin/timeout'
need "aviso: timeout blindado en el repo"  'tools/gnubin'
need "caso: monitorear un modulo"          'Monitorear un módulo por sí solo'
need "caso: fallo de ventana (Wayland)"    'WAYLAND_DEBUG=1'
need "caso: escape X11"                    'EWGUI_FORCE_X11=1'
need "caso: validar el .d sin ventana"     'csnhypodbp --print-config csnhypodbp.d'
need "caso: modulos de anillos headless"   'csnloc csnloc.d'
need "caso: fase W on-demand"              'csnwphase csnwphase.d --print-config'
need "caso: inspeccionar anillos"          'sniffring -n PICK_RING'

if [ "$fail" -eq 0 ]; then
    echo "OK: AGENTS.md documenta el arranque de modulos (receta + casos de uso)"
fi
exit $fail
