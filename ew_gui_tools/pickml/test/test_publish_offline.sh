#!/bin/sh
# test_publish_offline.sh — guardas de modo, publicación y log, SIN anillos.
#
# Verifica lo que no necesita EarthWorm arriba:
#   - sin flags de fuente el modo es VIVO (como `csnloc <d>`/`pickS <d>`).
#   - dos fuentes siguen siendo error ("elegir exactamente un modo").
#   - PublishRing 1 fuera del vivo es error (no se publica re-procesando).
#   - --publish-dry-run exige PublishRing 1 y modo vivo.
#   - un OutRing inexistente es fail-fast (exit 1) antes de abrir el motor.
#   - --print-config muestra las claves nuevas.
#   - el directorio de salida se crea si falta; si no se puede, exit 4.
#   - logit: LogFile 2 escribe EW_LOG/pickml_*.log y LogFile 0 no escribe.
#
# Este script AÍSLA EW_LOG en un directorio propio: ningún caso escribe en el
# log de producción (pasó una vez y ensució run_working_v8/log/pickml_*.log).
#
# El caso vivo (picks llegando al anillo) lo cubre `make test-live`.

set -e

cd "$(dirname "$0")/.."
TMP=test/tmp_pub
REPO=$(cd ../.. && pwd)
PARAMS="$REPO/run_working_v8/params"
export EW_PARAMS="$PARAMS"   # GetKey lee earthworm.d de aquí

rm -rf "$TMP"
mkdir -p "$TMP/log"

# Todo el script corre con su propio EW_LOG (ver la cabecera).
export EW_LOG="$TMP/log"

cat > "$TMP/sta" <<'EOF'
# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
1 1 TEST XX -- HHE HHN HHZ
EOF

# Base común: stub (sin ONNX), una estación, salida a archivo.
mk_d() {  # mk_d <ruta> [lineas extra...]
    f=$1; shift
    cat > "$f" <<EOF
MyModuleId     MOD_PICKML
InRing         SLINK_RING
OutRing        PICK_RING
StaFile        $TMP/sta
Engine         stub
PickOutFile    $TMP/out.jsonl
EOF
    for l in "$@"; do printf '%s\n' "$l" >> "$f"; done
}

# Cambia el valor de una clave ya presente (kom con la clave repetida no es
# fiable, así que se reemplaza la línea en vez de añadir otra).
set_key() {  # set_key <d> <clave> <valor>
    sed -i "s|^$2[[:space:]].*|$2 $3|" "$1"
}

fails=0

# expect <rc esperado> <texto esperado|-> <desc> <cmd...>
expect() {
    want_rc=$1; want_txt=$2; desc=$3; shift 3
    set +e
    out=$("$@" 2>&1)
    rc=$?
    set -e
    if [ "$rc" -ne "$want_rc" ]; then
        echo "FAIL $desc: rc=$rc (esperado $want_rc)"
        printf '%s\n' "$out" | sed 's/^/    /'
        fails=$((fails + 1))
        return 0
    fi
    if [ "$want_txt" != "-" ] && ! printf '%s' "$out" | grep -q "$want_txt"; then
        echo "FAIL $desc: falta '$want_txt'"
        printf '%s\n' "$out" | sed 's/^/    /'
        fails=$((fails + 1))
        return 0
    fi
    echo "PASS $desc (rc=$rc)"
}

# 1. publicar en modo offline es error de uso.
mk_d "$TMP/off.d" "PublishRing 1"
expect 2 "vale en modo --ring" \
    "PublishRing 1 con --traces es error" \
    ./pickml "$TMP/off.d" --traces "$TMP/no_hace_falta.trace"

# 2. --publish-dry-run sólo tiene sentido publicando en vivo.
mk_d "$TMP/dry0.d"
expect 2 "publish-dry-run exige PublishRing 1" \
    "--publish-dry-run sin PublishRing 1 es error" \
    ./pickml "$TMP/dry0.d" --ring --publish-dry-run

mk_d "$TMP/dryoff.d" "PublishRing 1"
expect 2 "publish-dry-run exige PublishRing 1" \
    "--publish-dry-run offline es error" \
    ./pickml "$TMP/dryoff.d" --traces "$TMP/no_hace_falta.trace" --publish-dry-run

# 3. anillo inexistente: fail-fast antes del motor (exit 1).
mk_d "$TMP/noring.d" "PublishRing 1" "OutRing NO_EXISTE_ESTE_ANILLO"
expect 1 "no esta declarado" \
    "OutRing inexistente es fail-fast" \
    ./pickml "$TMP/noring.d" --ring

# 4. --print-config expone las claves (lo que consume --validate).
mk_d "$TMP/show.d"
out=$(./pickml "$TMP/show.d" --print-config)
for k in "PublishRing     0" "PublishPhases   PS" "PublishMaxWeight 3"; do
    if printf '%s\n' "$out" | grep -qF "$k"; then
        echo "PASS --print-config incluye '$k'"
    else
        echo "FAIL --print-config no incluye '$k'"
        fails=$((fails + 1))
    fi
done

# 5. logit: LogFile 2 -> EW_LOG/pickml_*.log ; LogFile 0 -> nada.
mk_d "$TMP/log2.d" "PublishRing 1" "LogFile 2"
expect 2 "-" \
    "LogFile 2 + error de config" \
    ./pickml "$TMP/log2.d" --traces "$TMP/no_hace_falta.trace"
if ls "$TMP/log"/pickml_*.log >/dev/null 2>&1; then
    echo "PASS LogFile 2 escribe EW_LOG/pickml_*.log"
else
    echo "FAIL LogFile 2 no escribio EW_LOG/pickml_*.log"
    fails=$((fails + 1))
fi

rm -rf "$TMP/log"
mkdir -p "$TMP/log"
mk_d "$TMP/log0.d" "PublishRing 1" "LogFile 0"
expect 2 "vale en modo --ring" \
    "LogFile 0 + error de config" \
    ./pickml "$TMP/log0.d" --traces "$TMP/no_hace_falta.trace"
if ls "$TMP/log"/pickml_*.log >/dev/null 2>&1; then
    echo "FAIL LogFile 0 creo un log en EW_LOG"
    fails=$((fails + 1))
else
    echo "PASS LogFile 0 no escribe archivo"
fi

# 6. sin flags de fuente el modo es VIVO (como `csnloc <d>`/`pickS <d>`): con un
#    InRing inexistente el fallo tiene que ser del anillo (exit 4), NUNCA el
#    error de modo (exit 2) que dejaba a pickml muerto bajo startstop.
mk_d "$TMP/live_default.d"
set_key "$TMP/live_default.d" InRing NO_EXISTE_IN_RING
set +e
out=$(./pickml "$TMP/live_default.d" 2>&1)
rc=$?
set -e
if [ "$rc" -ne 4 ]; then
    echo "FAIL sin flags de fuente: rc=$rc (esperado 4: fallo de anillo, no de modo)"
    printf '%s\n' "$out" | sed 's/^/    /'
    fails=$((fails + 1))
elif printf '%s' "$out" | grep -q "exactamente un modo"; then
    echo "FAIL sin flags de fuente sigue dando error de modo"
    printf '%s\n' "$out" | sed 's/^/    /'
    fails=$((fails + 1))
else
    echo "PASS sin flags de fuente el modo es vivo (rc=4 por el anillo)"
fi

# ...y dos fuentes siguen siendo error de uso.
expect 2 "exactamente un modo" \
    "dos fuentes siguen siendo error" \
    ./pickml "$TMP/live_default.d" --traces "$TMP/x.traces" --sds "$TMP/y"

# 7. el directorio de salida se crea si falta (antes moría con exit 4 en un
#    worktree nuevo, donde run_working_v8/pickml/ no existe).
./test/gen_traces --traces "$TMP/burst.traces" --epoch 1700000000 --dur 60
mk_d "$TMP/nested.d"
set_key "$TMP/nested.d" PickOutFile "$TMP/a/b/c/out.jsonl"
expect 0 "-" \
    "crea el directorio anidado de PickOutFile" \
    ./pickml "$TMP/nested.d" --traces "$TMP/burst.traces" --stats
if [ -f "$TMP/a/b/c/out.jsonl" ]; then
    echo "PASS PickOutFile quedo escrito bajo $TMP/a/b/c/"
else
    echo "FAIL no se creo $TMP/a/b/c/out.jsonl"
    fails=$((fails + 1))
fi

# 8. ruta imposible: no arranca a medias, sale 4 diciendo por qué.
mk_d "$TMP/badout.d"
set_key "$TMP/badout.d" PickOutFile /proc/x/y/out.jsonl
expect 4 "no se pudo crear el directorio de salida" \
    "directorio de salida imposible" \
    ./pickml "$TMP/badout.d" --traces "$TMP/burst.traces" --stats

# 9. invariante del propio script: EW_LOG se aísla antes del primer caso.
first_pickml=$(grep -n '\./pickml' "$0" | head -1 | cut -d: -f1)
export_line=$(grep -n '^export EW_LOG=' "$0" | head -1 | cut -d: -f1)
if [ -n "$export_line" ] && [ -n "$first_pickml" ] &&
   [ "$export_line" -lt "$first_pickml" ]; then
    echo "PASS el script aísla EW_LOG antes del primer caso"
else
    echo "FAIL el script no aísla EW_LOG antes de los casos (ensucia el log real)"
    fails=$((fails + 1))
fi

rm -rf "$TMP"
if [ "$fails" -ne 0 ]; then
    echo "test_publish_offline: $fails fallos"
    exit 1
fi
echo "test_publish_offline: OK"
