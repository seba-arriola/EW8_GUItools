#!/usr/bin/env bash
# Guardián de `run_working_v8/params/`: SOLO configuración versionada.
#
# Ningún módulo NUESTRO escribe ahí: cada uno tiene su carpeta propia en
# `run_working_v8/` (runtime, ignorada en git). Este test fija ese invariante:
#
#   1. No puede haber ficheros NO versionados "a la vista" en params/.
#   2. Los ficheros ignorados solo pueden ser las salvedades declaradas: estado
#      interno de binarios de FÁBRICA (slink2ew -> slink*.state, pick_FP ->
#      pick_FP_*.ndx, sin clave de ruta en su .d), respaldos/temporales del
#      editor de `ew_controller` (<mod>.d.bak/.tmp) y el legado declarado.
#   3. Las claves de ESCRITURA (WorkDir, MwwWorkDir, StoreRoot) son RELATIVAS y
#      apuntan FUERA de params/.
#   4. Solo una lista cerrada de claves puede llevar ruta ABSOLUTA, todas de
#      LECTURA (recursos del disco de datos), y esas rutas deben existir. Una
#      clave nueva con ruta absoluta obliga a decidir si es de escritura.
#   5. Las entradas referenciadas por los refinadores (`CommandFile` -> `.hyp` ->
#      `STA`/`CRH`) resuelven desde su `WorkDir`.
#
# Se engancha en `make -C ew_gui_tools/common check`.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PARAMS="run_working_v8/params"

pass=0
fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }
warn() { echo "WARN $1"; }

cd "$ROOT" || { echo "no puedo cd a $ROOT"; exit 2; }
[ -d "$PARAMS" ] || { echo "no existe $PARAMS"; exit 2; }

# `cfg` imprime el .d sin comentarios ni lineas vacias.
cfg() { sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' -e 's/^[[:space:]]*//' "$1"; }

# --- 1. ficheros no versionados "a la vista" -------------------------------
stray="$(git ls-files --others --exclude-standard "$PARAMS")"
if [ -z "$stray" ]; then
    ok "params/: ningun fichero no versionado a la vista"
else
    bad "params/: hay ficheros no versionados (deberian ir a la carpeta del modulo)"
    echo "$stray" | sed 's/^/       /'
fi

# --- 2. ignorados: solo las salvedades declaradas --------------------------
badig=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    rel="${f#"$PARAMS"/}"
    case "$rel" in
        db.env)                              ;;  # credenciales (de db.env.example)
        earthworm.d)                         ;;  # generado por worktree_bootstrap.sh
        _legacy_atwc|_legacy_atwc/*)         ;;  # legado declarado
        hyp2000_output|hyp2000_output/*)     ;;  # legado (ncal.hyp, retirado)
        csnhypodbp_hist.txt)                 ;;  # residuo: nada lo escribe
        *.state|pick_FP_*.ndx)               ;;  # estado de binarios de FABRICA
        *.bak|*.bak.*|*.bak_*|*.tmp|*.log)   ;;  # respaldos/temporales
        *) badig=1; echo "       ignorado NO declarado: $rel" ;;
    esac
done < <(git ls-files --others --ignored --exclude-standard "$PARAMS")
if [ "$badig" -eq 0 ]; then
    ok "params/: los ignorados son solo las salvedades declaradas"
else
    bad "params/: hay ignorados no declarados (¿alguien escribe en params/?)"
fi

# --- 3. claves de ESCRITURA: relativas y fuera de params/ -----------------
wbad=0
for d in "$PARAMS"/*.d; do
    [ -f "$d" ] || continue
    while read -r key val _; do
        case "$key" in
            WorkDir|MwwWorkDir|StoreRoot)
                case "$val" in
                    /*)          echo "       $d: $key ABSOLUTA ($val)"; wbad=1 ;;
                    ../params|../params/*) echo "       $d: $key dentro de params/ ($val)"; wbad=1 ;;
                    ../*)        ;;
                    *)           echo "       $d: $key no es '../<algo>' ($val)"; wbad=1 ;;
                esac
                ;;
        esac
    done < <(cfg "$d")
done
if [ "$wbad" -eq 0 ]; then
    ok "claves de escritura relativas y fuera de params/ (WorkDir/MwwWorkDir/StoreRoot)"
else
    bad "hay claves de escritura absolutas o apuntando a params/"
fi

# --- 4. rutas absolutas: lista blanca de claves de LECTURA + existencia ----
ABS_KEYS=" SdsRoot ResponseDir CalibFile StaCorrFile Mb_QTable OverlayFile ProfileDataDir WphaseHome WphaseBin GfDir TtimeRoot ControlFile ModelFallback ModelBand ModelFile OrtLib "
# Claves cuyo valor NO es una ruta (aunque pueda parecerlo) y fuentes OPCIONALES
# (una fuente ausente es legitima: el codigo la tolera y sigue).
NOPATH_KEYS=" SetEnvVariable "
# ModelFile/OrtLib son recursos pesados fuera del repo (como GfDir): si no
# estan descargados, pickml arranca igual y falla al abrir el motor con texto.
OPTIONAL_KEYS=" SdsRoot ModelFile OrtLib "
abad=0
for d in "$PARAMS"/*.d; do
    [ -f "$d" ] || continue
    while read -r line; do
        read -r -a tok <<< "$line"
        key="${tok[0]}"
        wl=0
        case " $ABS_KEYS " in *" $key "*) wl=1 ;; esac
        byp=0
        case " $NOPATH_KEYS " in *" $key "*) byp=1 ;; esac
        [ "$byp" -eq 1 ] && continue
        op=0
        case " $OPTIONAL_KEYS " in *" $key "*) op=1 ;; esac
        for v in "${tok[@]:1}"; do
            case "$v" in
                /*)
                    if [ "$wl" -eq 0 ]; then
                        echo "       $d: clave '$key' con ruta ABSOLUTA fuera de la lista blanca: $v"
                        abad=1
                    elif [ ! -e "$v" ] && ! ls "$v"* >/dev/null 2>&1; then
                        # TtimeRoot/TtimeRoot de banda son PREFIJOS (<prefijo>.P.<STA>.time.hdr)
                        if [ "$op" -eq 1 ]; then
                            warn "$d: $key -> fuente ausente (opcional): $v"
                        else
                            echo "       $d: $key -> ruta/prefijo que NO existe: $v"
                            abad=1
                        fi
                    fi
                    ;;
            esac
        done
    done < <(cfg "$d")
done
if [ "$abad" -eq 0 ]; then
    ok "solo claves de lectura declaradas llevan ruta absoluta (y existen)"
else
    bad "rutas absolutas no declaradas o inexistentes (¿de escritura? ¿movidas?)"
fi

# --- 5. entradas de los refinadores: CommandFile -> .hyp -> STA/CRH -------
cbad=0
for d in "$PARAMS"/*.d; do
    [ -f "$d" ] || continue
    cmd="$(cfg "$d" | sed -n 's/^CommandFile[[:space:]]\{1,\}\([^[:space:]]*\).*$/\1/p' | head -1)"
    [ -n "$cmd" ] || continue
    wd="$(cfg "$d" | sed -n 's/^WorkDir[[:space:]]\{1,\}\([^[:space:]]*\).*$/\1/p' | head -1)"
    hyp="$PARAMS/$cmd"
    if [ ! -f "$hyp" ]; then
        echo "       $d: CommandFile no existe: $cmd"; cbad=1; continue
    fi
    while read -r k v; do
        p="$(printf '%s' "$v" | tr -d "'\"")"
        [ -n "$p" ] || continue
        case "$p" in
            /*) [ -e "$p" ] || { echo "       $hyp: $k -> no existe: $p"; cbad=1; } ;;
            # `realpath -m` normaliza los `..` SIN exigir que exista el WorkDir:
            # en un checkout/worktree limpio ese directorio runtime aun no existe
            # (lo crea el modulo al arrancar) y el kernel no puede atravesar un
            # `..` por el, aunque el destino final si exista.
            *)  [ -e "$(realpath -m -- "$PARAMS/$wd/$p")" ] || { echo "       $hyp: $k -> no resuelve desde $wd: $p"; cbad=1; } ;;
        esac
    done < <(sed "s/'//g" "$hyp" | grep -E '^(STA|CRH)[[:space:]]' | awk '{print $1" "$NF}')
done
if [ "$cbad" -eq 0 ]; then
    ok "las entradas del .hyp (STA/CRH) resuelven desde el WorkDir del modulo"
else
    bad "hay entradas del .hyp que no resuelven"
fi

# --- 6. las carpetas de los modulos NO deben estar versionadas -------------
tracked="$(git ls-files run_working_v8/hyp2000_ring run_working_v8/nlloc_ring run_working_v8/csnhypodbp)"
if [ -z "$tracked" ]; then
    ok "las carpetas runtime de los modulos (hyp2000_ring/nlloc_ring/csnhypodbp) no se versionan"
else
    bad "hay ficheros versionados en las carpetas runtime de los modulos"
    echo "$tracked" | sed 's/^/       /'
fi

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
