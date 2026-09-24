#!/bin/bash
# =============================================================================
#  test_chan_align.sh - coherencia del codigo de canal (HHZ/BHZ)
#
#  Diagnostico que verifican estas pruebas:
#    La fuente real (slink2ew) y pick_FP son HHZ, pero estaciones_107.txt y los
#    tanks de wave_serverV.d eran BHZ. Consecuencia:
#      - csnmags_toy descarta el 100% de las fases EN SILENCIO (strcmp exacto de
#        sta+net+chan; csnmagsutils.c:58-68 + csnmags_toy.c:294) -> nunca calcula.
#      - wave_serverV no almacena nada (FindSCNL exige el canal; wave_serverV.c:2939).
#
#  OFFLINE:
#    C0  align_channels.sh sobre una copia: alinea, es idempotente y dry-run no toca
#    C1  invariante: todo (STA,CHAN) que pick_FP puede emitir y csnloc conoce debe
#        estar en estaciones_107.txt Y en wave_serverV.d
#
#  LIVE (TANK_TESTS_LIVE=1):
#    C2  config SIN alinear + fixture HH -> el ARC llega y NO se acepta ninguna fase
#    C3  config alineada a HHZ + fixture HH -> se calcula y se emite TYPE_MAGNITUDE
#    C4  config alineada a BHZ + fixture BH -> idem (prueba POR INVERSION: lo unico
#        que cambia entre C3 y C4 es el codigo de canal)
#    C5  en los tres casos csnloc localiza (no depende del canal)
# =============================================================================
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

PARAMS="$ROOT_DIR/run_working_v8/params"
ALIGN="$ROOT_DIR/tank_tools/align_channels.sh"
NODATA=""
T0=$(( EVENT_EPOCH - FIXTURE_LEAD ))
MASTER="$TANKS_REAL/master.tank"

awk_data() { awk '!/^[[:space:]]*#/ && NF>0'; }   # ignora comentarios y vacias

# -----------------------------------------------------------------------------
echo "=== C0: align_channels.sh sobre una copia (offline) ==="
C0="$TESTTMP/c0"
make_params_copy "$C0" hhz >/dev/null
check_eq "estaciones_107.txt -> todo HHZ" \
    "$(awk_data < "$C0/estaciones_107.txt" | awk 'NF>=8 {print $3}' | sort -u | tr '\n' ' ')" "HHZ "
check_eq "pick_FP.sta -> todo HHZ" \
    "$(awk_data < "$C0/pick_FP.sta" | awk 'NF>=5 {print $4}' | sort -u | tr '\n' ' ')" "HHZ "
check_eq "wave_serverV.d Tank -> todo HHZ" \
    "$(awk '$1=="Tank" {print $3}' "$C0/wave_serverV.d" | sort -u | tr '\n' ' ')" "HHZ "

"$ALIGN" --params-dir "$C0" --mode hhz --apply >/dev/null 2>&1
check_exit "2a aplicacion -> exit 2 (idempotente)" 2 "$?"

C0B="$TESTTMP/c0b"
make_params_copy "$C0B" bhz >/dev/null            # copia coherente en BHZ
h1="$(md5sum "$C0B/estaciones_107.txt" | cut -d' ' -f1)"
"$ALIGN" --params-dir "$C0B" --mode hhz >/dev/null 2>&1
check_exit "dry-run sobre config BHZ -> exit 0 (cambios pendientes)" 0 "$?"
check_eq "dry-run no modifica nada" "$h1" "$(md5sum "$C0B/estaciones_107.txt" | cut -d' ' -f1)"
"$ALIGN" --params-dir "$C0B" --mode hhz --apply >/dev/null 2>&1
check_exit "apply sobre config BHZ -> exit 0" 0 "$?"
check_eq "tras apply queda HHZ" "HHZ" "$(awk_data < "$C0B/estaciones_107.txt" | awk 'NF>=8 {print $3}' | sort -u | tr -d '\n')"

# -----------------------------------------------------------------------------
echo "=== C1: invariante de coherencia (config real) ==="
if [ ! -f "$PARAMS/estaciones_107.txt" ] || [ ! -f "$PARAMS/wave_serverV.d" ]; then
    skip "C1 (faltan ficheros de configuracion)"
else
    T="$TESTTMP"
    awk_data < "$PARAMS/estaciones_107.txt" | awk 'NF>=8 {print $1}' | sort -u > "$T/est_sta"
    awk_data < "$PARAMS/estaciones_107.txt" | awk 'NF>=8 {print $1" "$3}' | sort -u > "$T/est_sc"
    awk '$1=="Tank" && NF>=11 {print $2" "$3}' "$PARAMS/wave_serverV.d" | sort -u > "$T/wsv_sc"
    # pares (STA,CHAN) que pick_FP puede emitir, restringidos a las STA que csnloc conoce
    awk_data < "$PARAMS/pick_FP.sta" | awk 'NF>=5 {print $3" "$4}' | sort -u \
        | awk -v f="$T/est_sta" 'BEGIN { while ((getline l < f) > 0) known[l] = 1 }
                                 ($1 in known) { print }' > "$T/pickfp_sc"

    total="$(wc -l < "$T/pickfp_sc" | tr -d ' ')"
    nopick="$(awk_data < "$PARAMS/pick_FP.sta" | awk 'NF>=5 {print $3}' | sort -u | wc -l | tr -d ' ')"
    echo "     pick_FP.sta: $nopick STA; de ellas $total son conocidas por estaciones_107.txt"
    conformes="$(awk -v fe="$T/est_sc" -v fw="$T/wsv_sc" '
        BEGIN { while ((getline l < fe) > 0) e[l] = 1
                while ((getline l < fw) > 0) w[l] = 1 }
        { if (($0 in e) && ($0 in w)) n++ }
        END { print n + 0 }' "$T/pickfp_sc")"
    echo "     conformes (presentes en metadata Y en tanks): $conformes/$total"
    if [ "$conformes" != "$total" ]; then
        echo "     pares INCUMPLIDOS (primeros 8):"
        awk -v fe="$T/est_sc" -v fw="$T/wsv_sc" '
            BEGIN { while ((getline l < fe) > 0) e[l] = 1
                    while ((getline l < fw) > 0) w[l] = 1 }
            { if (!(($0 in e) && ($0 in w))) print "       " $0 }' "$T/pickfp_sc" | head -8
        echo "     -> arreglo: ./tank_tools/align_channels.sh --apply"
    fi
    check_eq "todos los (STA,CHAN) de pick_FP estan en metadata y tanks" "$total" "$conformes"
fi

# -----------------------------------------------------------------------------
echo "=== C2: config SIN alinear + fixture HH (LIVE) ==="
if require_live "C2"; then
    if [ ! -f "$MASTER" ]; then
        skip "C2 (no existe $MASTER)"
    else
        HH="$TESTTMP/c_hh.tank"
        if tank_cut_at "$MASTER" "$HH" "$T0" 150 -C HH; then
            pdir="$TESTTMP/c2p"; ldir="$TESTTMP/c2l"
            make_params_copy "$pdir" mismatch "$TESTTMP/c2tanks" >/dev/null
            live_play "$HH" "$pdir" "$ldir" --mode realtime >/dev/null
            cml="$(latest_log "$ldir" csnmags_toy)"
            csl="$(latest_log "$ldir" csnloc)"
            # PRECONDICION DURA: si csnmags_toy no llega a su bucle, C2 pasaria
            # VAClamente (un modulo muerto no acepta fases ni emite magnitudes).
            # Hoy FALLA de forma determinista solo en la config 'mismatch':
            # queda como item abierto a investigar.
            check_contains "PRECONDICION: csnmags_toy arranco y entro en su bucle" \
                "$cml" "Listo. Esperando sismos"
            echo "     (info: traza de ARC en csnmags_toy = $(grep -c 'ACTUALIZACION GLASS3' "$cml" 2>/dev/null || echo 0))"
            check_not_contains "NINGUNA fase aceptada (bug de canal)" "$cml" '-> \['
            check_not_contains "csnmags_toy NO emite magnitud de red" "$cml" "CSNmags_Red"
            echo "     (info: TYPE_MAGNITUDE en HYPO_RING = $(ring_count_type HYPO_RING 28);"
            echo "      puede venir de un anillo reutilizado: los rings sobreviven si"
            echo "      startstop muere con SIGKILL. Ver 'ipcs -m')"
            check_contains "C5: csnloc localiza igualmente" "$csl" "evento"
            live_stop "$pdir"
        else
            failed "no se pudo generar el fixture -C HH"
        fi
    fi
fi

# -----------------------------------------------------------------------------
echo "=== C3: config alineada a HHZ + fixture HH (LIVE) ==="
if require_live "C3"; then
    if [ ! -f "$MASTER" ]; then
        skip "C3 (no existe $MASTER)"
    else
        HH="$TESTTMP/c_hh.tank"
        tank_cut_at "$MASTER" "$HH" "$T0" 150 -C HH || failed "no se pudo generar el fixture -C HH"
        if [ -s "$HH" ]; then
            pdir="$TESTTMP/c3p"; ldir="$TESTTMP/c3l"
            make_params_copy "$pdir" hhz "$TESTTMP/c3tanks" >/dev/null
            live_play "$HH" "$pdir" "$ldir" --mode realtime >/dev/null
            cml="$(latest_log "$ldir" csnmags_toy)"
            csl="$(latest_log "$ldir" csnloc)"
            check_contains "csnmags_toy resuelve estaciones" "$cml" '-> \['
            check_contains "emite magnitud de red" "$cml" "CSNmags_Red"
            echo "     (info: TYPE_MAGNITUDE en HYPO_RING = $(ring_count_type HYPO_RING 28))"
            check_contains "C5: csnloc localiza" "$csl" "evento"
            live_stop "$pdir"
        fi
    fi
fi

# -----------------------------------------------------------------------------
echo "=== C4: config alineada a BHZ + fixture BH (LIVE, por inversion) ==="
if require_live "C4"; then
    if [ ! -f "$MASTER" ]; then
        skip "C4 (no existe $MASTER)"
    else
        BH="$TESTTMP/c_bh.tank"
        tank_cut_at "$MASTER" "$BH" "$T0" 150 -C BH || failed "no se pudo generar el fixture -C BH"
        if [ -s "$BH" ]; then
            pdir="$TESTTMP/c4p"; ldir="$TESTTMP/c4l"
            make_params_copy "$pdir" bhz "$TESTTMP/c4tanks" >/dev/null
            live_play "$BH" "$pdir" "$ldir" --mode realtime >/dev/null
            cml="$(latest_log "$ldir" csnmags_toy)"
            csl="$(latest_log "$ldir" csnloc)"
            check_contains "csnmags_toy resuelve estaciones" "$cml" '-> \['
            check_contains "emite magnitud de red" "$cml" "CSNmags_Red"
            echo "     (info: TYPE_MAGNITUDE en HYPO_RING = $(ring_count_type HYPO_RING 28))"
            check_contains "C5: csnloc localiza" "$csl" "evento"
            live_stop "$pdir"
        fi
    fi
fi

summary
