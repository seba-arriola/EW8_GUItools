#!/bin/bash
# =============================================================================
#  test_fastmode.sh - pruebas del modo fast y del limite de 900 s de wave_serverV
#
#  OFFLINE (siempre):
#    F1  spans de los fixtures: el criterio es el SPAN, no el -d
#    F2  el span no lo fija -d (granularidad de paquete) + spans de los chunks
#    F3  mapeo --mode -> InterMessageDelayMillisecs (via --dry-run)
#
#  LIVE (TANK_TESTS_LIVE=1; arrancan EarthWorm y lo dejan detenido):
#    F4  fast dentro del limite: acelera, 0 rechazos, csnloc localiza
#    F5  fast fuera del limite: wave_serverV RECHAZA los paquetes con fecha futura
#    F6  --sequence 0 (re-ancla por fichero) vs --sequence 1 (acumula futuro)
# =============================================================================
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

MASTER="$TANKS_REAL/master.tank"
if [ ! -f "$MASTER" ]; then
    echo "== test_fastmode: SKIP (no existe $MASTER; corre mk_tank.sh antes) =="
    exit 0
fi

span_of() { tank_span "$1" 2>/dev/null | awk '{ print $3 + 0 }'; }

HH="$TESTTMP/hh.tank"; ALL="$TESTTMP/all.tank"
T0=$(( EVENT_EPOCH - FIXTURE_LEAD ))

# -----------------------------------------------------------------------------
echo "=== F1: spans de los fixtures (criterio = span, no -d) ==="
ok=1
tank_cut_at "$MASTER" "$HH"  "$T0" 150 -C HH || ok=0   # solo canales cortos
tank_cut_at "$MASTER" "$ALL" "$T0" 120       || ok=0   # todos los canales
check_exit "fixtures HH (-C HH) y ALL (todos) generados" 1 "$ok"
sp_hh="$(span_of "$HH")"
sp_all="$(span_of "$ALL")"
if [ "$ok" -eq 1 ]; then
    echo "     span(HH, -d 150)=${sp_hh}s   span(ALL, -d 120)=${sp_all}s"
    check_lt "span(HH) < 900  -> apto para fast" "$sp_hh" 900
    check_ge "span(ALL) >= 900 -> NO apto para fast" "$sp_all" 900
fi

# -----------------------------------------------------------------------------
echo "=== F2: el span no lo fija -d (granularidad de paquete) ==="
if [ "$ok" -eq 1 ]; then
    check_lt "un corte -d 120 con todos los canales dura MAS que -d 120" 120 "$sp_all"
    check_lt "filtrar canales cortos acorta el span" "$sp_hh" "$sp_all"
fi

for c in "$TANKS_REAL"/chunk_*.tank; do
    [ -e "$c" ] || continue
    s="$(span_of "$c")"
    [ -n "$s" ] || continue
    if [ "$s" -ge 900 ]; then
        echo "     AVISO: $(basename "$c") span=${s}s >= 900: en --mode fast se"
        echo "            su pauta sera infiel; en fast el limite lo marca -d, no el span"
    else
        echo "     $(basename "$c") span=${s}s -> apto para fast"
    fi
    check_ge "$(basename "$c"): span >= duracion pedida (paquete completo)" "$s" 600
done

# -----------------------------------------------------------------------------
echo "=== F3: mapeo --mode -> InterMessageDelayMillisecs (--dry-run) ==="
F3P="$TESTTMP/f3params"; F3L="$TESTTMP/f3log"
mkdir -p "$F3P" "$F3L"
cp -a "$ROOT_DIR/run_working_v8/params/tankplayer.d.tmpl"  "$F3P/" 2>/dev/null
cp -a "$ROOT_DIR/run_working_v8/params/startstop_replay.d" "$F3P/" 2>/dev/null

dry() { TANK_REPLAY_PARAMS="$F3P" TANK_REPLAY_LOG="$F3L" \
        "$TANK_REPLAY" play "$HH" "$@"; }

dry --mode realtime --dry-run            > "$F3L/rt.log"   2>&1
check_contains "realtime -> delay 0"          "$F3L/rt.log"   '^InterMessageDelayMillisecs 0'
check_contains "realtime -> SendLate presente" "$F3L/rt.log"  '^SendLate +30'

dry --mode fast --dry-run                > "$F3L/fast.log" 2>&1
check_contains "fast -> delay 10 por defecto"  "$F3L/fast.log" '^InterMessageDelayMillisecs 10'

dry --mode fast --delay-ms 3 --dry-run   > "$F3L/fast3.log" 2>&1
check_contains "--delay-ms gana al default"    "$F3L/fast3.log" '^InterMessageDelayMillisecs 3'

dry --mode realtime --sendlate none --dry-run > "$F3L/hist.log" 2>&1
check_not_contains "--sendlate none omite SendLate" "$F3L/hist.log" '^SendLate'

dry --mode malo --dry-run                > "$F3L/bad.log" 2>&1
check_exit "--mode invalido -> exit 1" 1 "$?"

# -----------------------------------------------------------------------------
echo "=== F4: fast DENTRO del limite (LIVE) ==="
if require_live "F4"; then
    pdir="$TESTTMP/f4p"; ldir="$TESTTMP/f4l"
    make_params_copy "$pdir" hhz "$TESTTMP/f4tanks" >/dev/null
    t0="$(date +%s)"
    log="$(live_play "$HH" "$pdir" "$ldir" --mode fast --delay-ms 1)"
    t1="$(date +%s)"; wall=$(( t1 - t0 ))
    echo "     pared=${wall}s   span=${sp_hh}s"
    check_lt "acelera: pared < span/2" "$wall" "$(( sp_hh / 2 ))"
    check_contains "tankplayer inyecta TYPE_TRACEBUF2" "$log" "Playing as type: 19"
    wsv="$(latest_log "$ldir" wave_serverV)"
    check_not_contains "wave_serverV NO rechaza nada" "$wsv" "fails validity check, discarding"
    csl="$(latest_log "$ldir" csnloc)"
    # En --mode fast las ventanas de csnloc se colapsan (todos los picks caen en la
    # misma ventana), asi que NO se exige localizacion: eso lo verifican C3/C4/C5
    # en modo realtime. Aqui solo se informa.
    echo "     (info: 'evento' en csnloc = $(grep -c 'evento' "$csl" 2>/dev/null || echo 0);"
    echo "      en fast la localizacion no es representativa)"
    live_stop "$pdir"
fi

# -----------------------------------------------------------------------------
echo "=== F5: fast FUERA del limite (LIVE) ==="
if require_live "F5"; then
    # wave_serverV rechaza por STARTTIME (wave_serverV.c:3024), no por endtime, y su
    # paquetes sean largos (1 sps) Y que tenga tank. Los canales HH (100 sps)
    # tienen paquetes de ~10 s y nunca se alejan 900 s del reloj.
    LONG="$TESTTMP/long.tank"
    if tank_cut_at "$MASTER" "$LONG" "$T0" 1100 -C LHZ; then
        sp_long="$(span_of "$LONG")"
        echo "     fixture LH: span=${sp_long}s"
        if [ "$sp_long" -lt 1200 ]; then
            skip "F5 (el corte -C LH no supera 1200 s de span; no se puede probar el limite con margen)"
        else
            pdir="$TESTTMP/f5p"; ldir="$TESTTMP/f5l"
            make_params_copy "$pdir" chan:LHZ "$TESTTMP/f5tanks" >/dev/null
            t0="$(date +%s)"
            live_play "$LONG" "$pdir" "$ldir" --mode fast --delay-ms 1 >/dev/null
            t1="$(date +%s)"; wall=$(( t1 - t0 ))
            echo "     pared=${wall}s   span=${sp_long}s"
            check_lt "acelera: pared < span/2" "$wall" "$(( sp_long / 2 ))"
            wsv="$(latest_log "$ldir" wave_serverV)"
            check_contains "wave_serverV RECHAZA los paquetes con fecha futura" \
                "$wsv" "fails validity check, discarding"
            echo "     SCNL descartados (muestra):"
            grep "fails validity check, discarding" "$wsv" 2>/dev/null | head -4 | sed 's/^/       /'
            live_stop "$pdir"
        fi
    else
        skip "F5 (no se pudo generar el corte -C LH)"
    fi
fi

# -----------------------------------------------------------------------------
echo "=== F6: --sequence 0 vs 1 (LIVE) ==="
if require_live "F6"; then
    SD="$TESTTMP/seq"; mkdir -p "$SD"
    # Dos cortes de canal corto separados 800 s en tiempo de dato:
    #   con --sequence 0 cada uno se re-ancla a "ahora"  -> sin rechazos
    #   con --sequence 1 el 2o continua al 1o (800+163 s) -> futuro > 900 s
    tank_cut_at "$MASTER" "$SD/chunk_000.tank" "$T0"           150 -C HH
    tank_cut_at "$MASTER" "$SD/chunk_001.tank" "$(( T0 + 800 ))" 150 -C HH
    pdir="$TESTTMP/f6p"
    make_params_copy "$pdir" hhz "$TESTTMP/f6tanks" >/dev/null

    ldir="$TESTTMP/f6l0"
    live_play "$SD" "$pdir" "$ldir" --mode fast --delay-ms 1 --sequence 0 >/dev/null
    wsv="$(latest_log "$ldir" wave_serverV)"
    # Con --sequence 0 el 2o fichero se RE-ANCLA a "ahora", es decir va HACIA
    # ATRAS respecto al final del 1o: wave_serverV lo rechaza por no avanzar
    # (wave_serverV.c:3018), no por fecha futura.
    check_contains "sequence 0: el 2o chunk re-anclado va hacia atras -> se rechaza" \
        "$wsv" "fails validity check, discarding"
    check_contains "sequence 0: motivo = no avanza el tiempo del tank" \
        "$wsv" "is before end of tank's last good packet"
    live_stop "$pdir"

    ldir="$TESTTMP/f6l1"
    live_play "$SD" "$pdir" "$ldir" --mode fast --delay-ms 1 --sequence 1 >/dev/null
    wsv="$(latest_log "$ldir" wave_serverV)"
    check_contains "sequence 1: acumula futuro -> rechaza" \
        "$wsv" "fails validity check, discarding"
    live_stop "$pdir"
fi

summary
