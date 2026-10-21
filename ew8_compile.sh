#!/bin/bash
# =============================================================================
#  ew8_compile.sh - compilacion, tests y validacion de EW8_GUItools
#  Proyecto: EW8_GUItools
#
#  QUE HACE
#  --------
#  Un unico punto de entrada para construir los modulos de ew_gui_tools/ contra
#  la instalacion vendorizada de EarthWorm 8, en el orden correcto y con el
#  toolkit GTK4 que exigen los modulos GUI. Ademas puede ejecutar, sobre los
#  modulos seleccionados, los tests headless y las validaciones de configuracion
#  que ya existen en cada Makefile. Traduce cada verbo al
#  target real del modulo (check, test, test-data, test-config, check-offline,
#  test-live, check-engine, check-corpus).
#
#  POR QUE EXISTE
#  --------------
#  La receta manual (ver AGENTS.md seccion 5) exige recordar que:
#    - common/ SIEMPRE se construye con GUI_TOOLKIT=gtk4 (su default es gtk3,
#      y con gtk3 la libreria se llama libewgui.a, no libewgui_gtk4.a);
#    - los modulos se construyen en un orden concreto (common y wavestore antes
#      que csnmags/csnhypodbp);
#    - `make all` ya instala en ew_8.0/bin (install_bin.sh, copia atomica: es
#      seguro compilar con EarthWorm corriendo);
#    - los tests no son homogeneos: unos modulos tienen `check`, otros `test`,
#      otros `test-config`, y solo algunos `check-offline`.
#  Este script encapsula todo eso para que compilar no dependa de memoria.
#
#  USO RAPIDO
#  ----------
#     ./ew8_compile.sh                          # compila e instala TODO
#     ./ew8_compile.sh all --check              # compila + tests headless
#     ./ew8_compile.sh csnloc                   # solo un modulo
#     ./ew8_compile.sh csnloc --check           # compila ese modulo y lo testea
#     ./ew8_compile.sh --no-build --check       # tests sin recompilar
#     ./ew8_compile.sh all --no-build --validate  # valida configuracion
#     ./ew8_compile.sh --list                   # que soporta cada modulo
#     ./ew8_compile.sh --help                   # esta ayuda
#
#  GRAMATICA
#  ---------
#     ./ew8_compile.sh [<target>...] [<verbo>...] [opciones]
#
#     <target>  all | libs | headless | gui | <modulo> [<modulo>...]
#               Sin target se asume `all`.
#               Modulos: common wavestore csnloc pickS csnmags csnwphase hypodb
#                        hyp2000_ring nlloc_ring csnhypodbp csnrv csntvp
#                        csnstaevdisp ew_controller
#               Alias: hypodb_ingest -> hypodb, wstore_ingest|wstore -> wavestore
#
#     <verbo>   --check      tests unitarios/estaticos headless
#               --test       integracion offline (check-offline)
#               --validate   validacion de configuracion (.d, perfiles, overlays)
#               --all-checks equivale a --check --test --validate
#               Sin verbo: solo compila.
#               Con verbo: compila PRIMERO (salvo --no-build) y luego ejecuta.
#
#  EJEMPLOS: COMPILAR
#  ------------------
#  1) Compilar e instalar todo, de forma incremental (lo normal del dia a dia).
#     `make all` de cada modulo ya incluye su target `install`, asi que al
#     terminar los binarios estan actualizados en ew_8.0/bin. Es seguro con
#     EarthWorm en marcha: install_bin.sh copia a un temporal y hace `mv`.
#
#        ./ew8_compile.sh
#        ./ew8_compile.sh all
#
#  2) Recompilar TODO desde cero. Antes de cada `make all` se ejecuta
#     `make clean` de ese modulo. Util tras cambiar headers de common/ o al
#     migrar de maquina. Es la opcion lenta.
#
#        ./ew8_compile.sh all --clean
#
#  3) Compilar e instalar UN solo modulo. El orden canonico y las dependencias
#     compartidas las resuelve el propio Makefile del modulo (sub-make a
#     common/, wavestore/, csnloc/, csnwphase/ cuando hacen falta).
#
#        ./ew8_compile.sh csnloc
#        ./ew8_compile.sh csnhypodbp        # compila ademas csnwphase/wavestore
#        ./ew8_compile.sh csnwphase
#        ./ew8_compile.sh pickS
#
#  4) Modulos cuyo BINARIO no se llama como la carpeta. El script acepta el
#     nombre de la carpeta o el del binario, y siempre reporta el canonico:
#        hypodb     -> instala hypodb_ingest
#        wavestore  -> instala wstore_ingest tank2store store2tank
#                              wstore_query wstore_prune
#
#        ./ew8_compile.sh hypodb
#        ./ew8_compile.sh hypodb_ingest       # equivalente
#        ./ew8_compile.sh wavestore
#        ./ew8_compile.sh wstore              # equivalente
#
#  5) Compilar por GRUPOS, sin listar modulo por modulo:
#        libs      -> common (la libreria libewgui_gtk4.a)
#        headless  -> wavestore csnloc pickS csnmags csnwphase hypodb
#                     hyp2000_ring nlloc_ring
#        gui       -> csnhypodbp csnrv csntvp csnstaevdisp ew_controller
#
#        ./ew8_compile.sh libs
#        ./ew8_compile.sh headless
#        ./ew8_compile.sh gui
#
#  6) Varios modulos concretos en una sola invocacion. Se deduplican y se
#     ejecutan en el orden canonico, no en el orden en que los escribas.
#
#        ./ew8_compile.sh csnloc pickS csnmags
#        ./ew8_compile.sh csnrv csntvp csnstaevdisp
#
#  7) Compilar en paralelo DENTRO de cada modulo (-j). Los modulos siguen
#     siendo secuenciales a proposito: dos sub-makes simultaneos a common/
#     pueden pisarse. El numero debe ser >= 1.
#
#        ./ew8_compile.sh all -j 4
#        ./ew8_compile.sh all --jobs=8
#        ./ew8_compile.sh csnhypodbp -j2
#
#  EJEMPLOS: TESTS (--check)
#  -------------------------
#  --check ejecuta el primer target disponible de: check -> test -> todos los
#  test-* menos test-live. Todo es headless: no necesita DISPLAY ni EarthWorm.
#
#  8) Compilar todo y correr los tests headless de todo (el "verde" completo).
#
#        ./ew8_compile.sh all --check
#
#  9) Tests de un modulo, recompilando antes:
#
#        ./ew8_compile.sh csnloc --check
#        ./ew8_compile.sh common --check
#        ./ew8_compile.sh csnrv --check          # csnrv no tiene `check`: usa `test`
#        ./ew8_compile.sh csnhypodbp --check     # idem: usa `test`
#        ./ew8_compile.sh csnstaevdisp --check   # corre test-data Y test-config
#        ./ew8_compile.sh ew_controller --check  # invariantes de fuente (rapido)
#
# 10) Correr SOLO los tests, sin recompilar nada. Util cuando acabas de
#     cambiar un test o un .d y el binario ya esta al dia.
#
#        ./ew8_compile.sh --no-build --check
#        ./ew8_compile.sh csnrv --no-build --check
#        ./ew8_compile.sh hypodb wavestore --no-build --check
#
# 11) Tests LENTOS y opt-in de csnwphase contra el motor real de fase W
#     (grid search completo y corpus historico). NO se ejecutan con --check a
#     secas: hay que pedirlos con --slow. Pueden tardar minutos por evento.
#
#        ./ew8_compile.sh csnwphase --check --slow
#        ./ew8_compile.sh csnwphase --no-build --check --slow
#
#  EJEMPLOS: INTEGRACION OFFLINE (--test)
#  --------------------------------------
#  --test ejecuta check-offline donde existe (csnloc, pickS, csnmags,
#  hyp2000_ring, nlloc_ring): reloj virtual, sin anillos ni red, resultado
#  determinista. En los modulos que no lo tienen reporta SKIP, que NO es fallo.
#
# 12) Integracion offline de la cadena de picado y localizacion:
#
#        ./ew8_compile.sh csnloc pickS csnmags --test
#        ./ew8_compile.sh hyp2000_ring nlloc_ring --test
#        ./ew8_compile.sh all --test
#
# 13) Seguir despues de un fallo para ver el panorama completo. Sin
#     --keep-going el script se detiene en el primer FAIL.
#
#        ./ew8_compile.sh all --test --keep-going
#
# 14) Solo offline, sin recompilar (los refinadores necesitan csnloc ya
#     instalado; si no lo esta, compilalo antes o quita --no-build):
#
#        ./ew8_compile.sh csnloc pickS csnmags --no-build --test
#
# 15) Tests que necesitan servicios o pantalla, todos opt-in:
#        --live  -> test-live de csnhypodbp y ew_controller.
#                   Requiere DISPLAY/WAYLAND_DISPLAY y EarthWorm en marcha.
#                   Si no hay pantalla, el paso queda SKIP (exit 0).
#        --db    -> hypodb: ejecuta `make check` con EWGUI_DB_TEST_ENV=1,
#                   es decir test_ingest_db contra PostgreSQL. Sin --db,
#                   hypodb reporta SKIP en --test (el test se autosalta).
#                   La base se levanta antes con ./ew8_db.sh up.
#
#        ./ew8_compile.sh csnhypodbp ew_controller --test --live
#        ./ew8_compile.sh ew_controller --no-build --test --live
#        ./ew8_compile.sh hypodb --no-build --test --db
#
#  EJEMPLOS: VALIDACION DE CONFIGURACION (--validate)
#  ---------------------------------------------------
#  --validate primero ejecuta check_install.sh -v (compara instalado contra
#  local y avisa de binarios ausentes o desfasados) y luego corre los flags de
#  validacion de cada binario INSTALADO, con cwd=run_working_v8/params para que
#  resuelvan sus rutas relativas. Respeta el orden de argumentos de cada
#  binario (unos llevan el flag antes del .d y csnwphase lo lleva despues).
#  Los modulos sin flags de validacion quedan en SKIP, no en FAIL.
#
#        csnhypodbp   --print-config csnhypodbp.d
#                     --check-profiles csnhypodbp.d
#        csnrv        --print-config csnrv.d
#                     --check-overlays csnrv.d
#        csnstaevdisp --check-overlays csnstaevdisp.d
#        csnwphase    csnwphase.d --print-config
#
# 16) Validar la configuracion de todo sin recompilar (rapido y solo lectura):
#
#        ./ew8_compile.sh all --no-build --validate
#
# 17) Validar solo un visor o el modulo de hipocentros:
#
#        ./ew8_compile.sh csnhypodbp --no-build --validate
#        ./ew8_compile.sh csnrv --no-build --validate
#        ./ew8_compile.sh csnstaevdisp --no-build --validate
#        ./ew8_compile.sh csnwphase --no-build --validate
#
# 18) Si un .d falta o un binario no esta instalado, el paso se marca FAIL con
#     la ruta esperada y la sugerencia de compilar ese modulo:
#
#        ./ew8_compile.sh csnhypodbp --validate      # compila+instala y valida
#
#  EJEMPLOS: COMBINACIONES
#  -----------------------
# 19) El "todo verde" de una tacada (compila una vez y luego los tres niveles):
#
#        ./ew8_compile.sh all --all-checks
#        ./ew8_compile.sh all --check --test --validate
#
# 20) Todo verde incluyendo lo lento y lo que necesita pantalla:
#
#        ./ew8_compile.sh all --all-checks --slow --live
#
# 21) Un modulo de punta a punta:
#
#        ./ew8_compile.sh csnloc --check --test
#        ./ew8_compile.sh csnhypodbp --check --validate
#        ./ew8_compile.sh csnwphase --check --slow --validate
#
#  EJEMPLOS: MIRAR SIN EJECUTAR Y SALIDA
#  -------------------------------------
# 22) Ver EXACTAMENTE que comandos se ejecutarian, en que orden y con que
#     argumentos. No carga el entorno, no ejecuta make, no crea directorios.
#
#        ./ew8_compile.sh all --check --test --validate --dry-run
#        ./ew8_compile.sh csnloc --clean --check --dry-run
#        ./ew8_compile.sh gui -j 4 --check --dry-run
#
# 23) Salida silenciosa: el output de cada paso va a un log temporal y solo se
#     muestra (las ultimas 40 lineas) si el paso falla. --keep-logs conserva
#     esos logs incluso cuando todo sale bien; sin el, se borran al terminar
#     (y se conservan automaticamente si hubo algun fallo).
#
#        ./ew8_compile.sh all --quiet
#        ./ew8_compile.sh all --quiet --keep-logs
#        ./ew8_compile.sh all --check --quiet --keep-logs
#
# 24) Descubrir que puede hacer cada modulo:
#
#        ./ew8_compile.sh --list
#        ./ew8_compile.sh --list | less
#
#  SALIDA
#  ------
#  Cada paso imprime una linea con su verbo, modulo y comando:
#     [build]  csnloc    make -C ew_gui_tools/csnloc -j1 all GUI_TOOLKIT=gtk4
#     [check]  csnloc    make -C ew_gui_tools/csnloc check GUI_TOOLKIT=gtk4
#     [skip]   csnrv     --test: sin target check-offline
#  y al final una tabla-resumen con OK / FAIL / SKIP, duracion y, si fallo, la
#  ruta del log. Los SKIP no alteran el codigo de salida.
#
#  REQUISITOS
#  ----------
#   - gcc, make, pkg-config.
#   - pkg-config gtk4 libadwaita-1 (GTK >= 4.14, libadwaita >= 1.5) si se
#     selecciona algun modulo GUI.
#   - libpq-dev para hypodb; libmseed2 viene vendorizada en ew_8.0/lib.
#   - ew8_unix.sh (lo carga el script si hace falta; si ya cargaste el entorno
#     de este repo, no lo vuelve a cargar).
#   - Para --validate: los .d en run_working_v8/params.
#   - Para --live: DISPLAY/WAYLAND_DISPLAY y EarthWorm arriba.
#   - Para --db: PostgreSQL levantado con ./ew8_db.sh up.
#
#  CODIGOS DE SALIDA
#  -----------------
#     0   todo OK (los SKIP no cuentan como fallo)
#     1   al menos un paso FAIL
#     2   uso invalido (target/opcion desconocida, -j invalido)
#     4   falta un prerrequisito (EW_HOME, make, pkg-config, GTK4, .d)
#     130 interrumpido con Ctrl-C / SIGTERM (se imprime resumen parcial)
#
#  NOTAS
#  -----
#   - `make all` de cada modulo instala en ew_8.0/bin. No hay flag para evitar
#     la instalacion: es el comportamiento de los Makefiles.
#   - Todas las invocaciones de make llevan GUI_TOOLKIT=gtk4, incluso en los
#     modulos headless, para que los sub-makes a common/ nunca caigan en gtk3.
#   - No se compila _legacy_atwc/ ni se ejecutan los arneses manuales `chain`,
#     `check-valgrind`, `smoke`, `repeat`, `asan-*` y `dbg-*`.
#   - Los pasos se ejecutan en orden canonico y de fase: primero todos los
#     build, luego los check, luego los test y por ultimo los validate.
# ===== FIN DE LA AYUDA

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARAMS_DIR="$ROOT/run_working_v8/params"

# -----------------------------------------------------------------------------
# Registro de modulos. Una fila por modulo; el orden del array ES el orden
# canonico de construccion (common y wavestore primero: los demas los enlazan).
# -----------------------------------------------------------------------------
MODULES=(
    common wavestore csnloc pickS csnmags csnwphase hypodb
    hyp2000_ring nlloc_ring csnhypodbp csnrv csntvp csnstaevdisp ew_controller
)
HEADLESS_MODULES=(wavestore csnloc pickS csnmags csnwphase hypodb hyp2000_ring nlloc_ring)
GUI_MODULES=(csnhypodbp csnrv csntvp csnstaevdisp ew_controller)

declare -A MOD_DIR=(
    [common]=ew_gui_tools/common
    [wavestore]=ew_gui_tools/wavestore
    [csnloc]=ew_gui_tools/csnloc
    [pickS]=ew_gui_tools/pickS
    [csnmags]=ew_gui_tools/csnmags
    [csnwphase]=ew_gui_tools/csnwphase
    [hypodb]=ew_gui_tools/hypodb
    [hyp2000_ring]=ew_gui_tools/hyp2000_ring
    [nlloc_ring]=ew_gui_tools/nlloc_ring
    [csnhypodbp]=ew_gui_tools/csnhypodbp
    [csnrv]=ew_gui_tools/csnrv
    [csntvp]=ew_gui_tools/csntvp
    [csnstaevdisp]=ew_gui_tools/csnstaevdisp
    [ew_controller]=ew_gui_tools/ew_controller
)

declare -A MOD_BINS=(
    [common]="(libewgui_gtk4.a)"
    [wavestore]="wstore_ingest tank2store store2tank wstore_query wstore_prune"
    [csnloc]="csnloc"
    [pickS]="pickS"
    [csnmags]="csnmags"
    [csnwphase]="csnwphase"
    [hypodb]="hypodb_ingest"
    [hyp2000_ring]="hyp2000_ring"
    [nlloc_ring]="nlloc_ring"
    [csnhypodbp]="csnhypodbp"
    [csnrv]="csnrv"
    [csntvp]="csntvp"
    [csnstaevdisp]="csnstaevdisp"
    [ew_controller]="ew_controller"
)

# --check: primer target en orden check > test > test-* (sin test-live).
declare -A MOD_CHECK=(
    [common]=check
    [wavestore]=check
    [csnloc]=check
    [pickS]=check
    [csnmags]=check
    [csnwphase]=check
    [hypodb]=check
    [hyp2000_ring]=check
    [nlloc_ring]=check
    [csnhypodbp]=test
    [csnrv]=test
    [csntvp]=test
    [csnstaevdisp]="test-data test-config"
    [ew_controller]=test
)

# --test: solo donde existe check-offline (hypodb se trata aparte con --db).
declare -A MOD_OFFLINE=(
    [common]=""
    [wavestore]=""
    [csnloc]=check-offline
    [pickS]=check-offline
    [csnmags]=check-offline
    [csnwphase]=""
    [hypodb]=""
    [hyp2000_ring]=check-offline
    [nlloc_ring]=check-offline
    [csnhypodbp]=""
    [csnrv]=""
    [csntvp]=""
    [csnstaevdisp]=""
    [ew_controller]=""
)

# -----------------------------------------------------------------------------
# Estado de la CLI
# -----------------------------------------------------------------------------
TARGETS=()
SELECTED=()
declare -A SEEN=()
DO_BUILD=0
DO_CHECK=0
DO_TEST=0
DO_VALIDATE=0
VERB_SEEN=0
CLEAN=0
NO_BUILD=0
KEEP_GOING=0
JOBS=1
QUIET=0
KEEP_LOGS=0
DRY_RUN=0
SLOW=0
LIVE=0
WITH_DB=0
SHOW_LIST=0
STOP=0
FAILED=0
LOGDIR=""

RES_KIND=()
RES_NAME=()
RES_STATUS=()
RES_MS=()
RES_LOG=()

# -----------------------------------------------------------------------------
# Utilidades
# -----------------------------------------------------------------------------
usage() {
    sed -n '2,/^# ===== FIN DE LA AYUDA$/{/^# ===== FIN DE LA AYUDA$/d;p}' \
        "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

die() {
    local code=$1
    shift
    echo "[error] $*" >&2
    exit "$code"
}

cleanup_logs() {
    if [ -n "$LOGDIR" ] && [ -d "$LOGDIR" ] && (( ! KEEP_LOGS )) && (( ! FAILED )); then
        rm -rf "$LOGDIR"
    fi
}

on_signal() {
    STOP=1
    echo "" >&2
    echo "[error] interrumpido por senal" >&2
    summary
    exit 130
}

trap cleanup_logs EXIT
trap on_signal INT TERM

# Nombre canonico de un target/modulo, o vacio si no existe.
canon_module() {
    local t=$1
    case "$t" in
        hypodb_ingest) t=hypodb ;;
        wstore|wstore_ingest) t=wavestore ;;
    esac
    if [ -n "${MOD_DIR[$t]+x}" ]; then
        printf '%s' "$t"
        return 0
    fi
    return 1
}

add_selected() {
    local m=$1
    [ -n "${SEEN[$m]+x}" ] && return 0
    SEEN[$m]=1
}

resolve_targets() {
    local t m
    if (( ${#TARGETS[@]} == 0 )); then
        TARGETS=(all)
    fi
    for t in "${TARGETS[@]}"; do
        case "$t" in
            all)
                for m in "${MODULES[@]}"; do add_selected "$m"; done
                ;;
            libs)
                add_selected common
                ;;
            headless)
                for m in "${HEADLESS_MODULES[@]}"; do add_selected "$m"; done
                ;;
            gui)
                for m in "${GUI_MODULES[@]}"; do add_selected "$m"; done
                ;;
            *)
                if ! m=$(canon_module "$t"); then
                    die 2 "target desconocido: '$t' (ver --list)"
                fi
                add_selected "$m"
                ;;
        esac
    done
    for m in "${MODULES[@]}"; do
        [ -n "${SEEN[$m]+x}" ] && SELECTED+=("$m")
    done
    (( ${#SELECTED[@]} > 0 )) || die 2 "no hay modulos seleccionados"
}

is_gui_selected() {
    local m g
    for m in "${SELECTED[@]}"; do
        for g in "${GUI_MODULES[@]}"; do
            [ "$m" = "$g" ] && return 0
        done
    done
    return 1
}

# -----------------------------------------------------------------------------
# Parseo de argumentos
# -----------------------------------------------------------------------------
parse_args() {
    while (( $# )); do
        case "$1" in
            -h|--help)
                usage 0
                ;;
            --list)
                SHOW_LIST=1
                shift
                ;;
            --build)
                DO_BUILD=1; VERB_SEEN=1; shift
                ;;
            --check)
                DO_CHECK=1; VERB_SEEN=1; shift
                ;;
            --test)
                DO_TEST=1; VERB_SEEN=1; shift
                ;;
            --validate)
                DO_VALIDATE=1; VERB_SEEN=1; shift
                ;;
            --all-checks)
                DO_CHECK=1; DO_TEST=1; DO_VALIDATE=1; VERB_SEEN=1; shift
                ;;
            --no-build)
                NO_BUILD=1; shift
                ;;
            --clean)
                CLEAN=1; shift
                ;;
            --keep-going)
                KEEP_GOING=1; shift
                ;;
            -q|--quiet)
                QUIET=1; shift
                ;;
            --keep-logs)
                KEEP_LOGS=1; shift
                ;;
            -n|--dry-run)
                DRY_RUN=1; shift
                ;;
            --slow)
                SLOW=1; shift
                ;;
            --live)
                LIVE=1; shift
                ;;
            --db)
                WITH_DB=1; shift
                ;;
            -j|--jobs)
                (( $# >= 2 )) || die 2 "$1 necesita un numero (ej.: -j 4)"
                JOBS=$2
                shift 2
                ;;
            -j*)
                JOBS=${1#-j}
                shift
                ;;
            --jobs=*)
                JOBS=${1#--jobs=}
                shift
                ;;
            --)
                shift
                while (( $# )); do TARGETS+=("$1"); shift; done
                ;;
            -*)
                die 2 "opcion desconocida: '$1' (ver --help)"
                ;;
            *)
                TARGETS+=("$1")
                shift
                ;;
        esac
    done

    [[ "$JOBS" =~ ^[0-9]+$ ]] && (( JOBS >= 1 )) \
        || die 2 "numero de jobs invalido: '$JOBS' (debe ser un entero >= 1)"

    if (( NO_BUILD && ! VERB_SEEN )); then
        die 2 "--no-build sin verbos (--check/--test/--validate) no hace nada"
    fi
    # Con verbo se compila primero, salvo --no-build. Sin verbo, solo compila.
    if (( NO_BUILD )); then
        DO_BUILD=0
    else
        DO_BUILD=1
    fi
}

# -----------------------------------------------------------------------------
# Entorno y prerrequisitos
# -----------------------------------------------------------------------------
ensure_env() {
    if [ "${EW_PARAMS:-}" = "$PARAMS_DIR" ] && [ "${EW_HOME:-}" = "$ROOT" ]; then
        return 0
    fi
    [ -r "$ROOT/ew8_unix.sh" ] || die 4 "falta $ROOT/ew8_unix.sh"
    local cwd=$PWD
    # ew8_unix.sh imprime un banner y hace `cd` a la raiz: silenciamos stdout y
    # restauramos el cwd. Dejamos stderr visible (avisos de timeout/GTK).
    # shellcheck disable=SC1090
    . "$ROOT/ew8_unix.sh" >/dev/null
    cd "$cwd" || cd "$ROOT" || true
    [ "${EW_HOME:-}" = "$ROOT" ] || die 4 "ew8_unix.sh no fijo EW_HOME=$ROOT"
    [ -n "${EW_VERSION:-}" ] || die 4 "EW_VERSION vacio tras cargar el entorno"
}

need_build_tools() {
    if (( DO_BUILD || DO_CHECK || DO_TEST )); then
        command -v make >/dev/null 2>&1 || die 4 "make no esta instalado"
        command -v pkg-config >/dev/null 2>&1 \
            || die 4 "pkg-config no esta instalado (lo necesitan los Makefiles)"
    fi
    if (( DO_BUILD || DO_CHECK || DO_TEST )) && is_gui_selected; then
        pkg-config --exists gtk4 libadwaita-1 2>/dev/null \
            || die 4 "faltan gtk4/libadwaita-1 (pkg-config): ver docs/GTK4-MIGRATION.md"
    fi
    if (( DO_BUILD )); then
        [ -d "$EW_HOME/$EW_VERSION/bin" ] \
            || die 4 "no existe $EW_HOME/$EW_VERSION/bin"
    fi
}

# -----------------------------------------------------------------------------
# Ejecucion de pasos
# -----------------------------------------------------------------------------
record() {
    RES_KIND+=("$1")
    RES_NAME+=("$2")
    RES_STATUS+=("$3")
    RES_MS+=("$4")
    RES_LOG+=("$5")
}

# emit_step KIND MODULE DESC CWD CMD...
# En --dry-run solo imprime DESC. En ejecucion corre CMD con cwd=CWD.
emit_step() {
    local kind=$1 module=$2 desc=$3 cwd=$4
    shift 4

    if (( DRY_RUN )); then
        printf '%-10s %-14s %s\n' "[$kind]" "$module" "$desc"
        return 0
    fi
    (( STOP )) && return 0

    local start end ms rc logfile=""
    start=$(date +%s%N)
    if (( QUIET )); then
        local safe=${module//[^A-Za-z0-9_.-]/_}
        logfile="$LOGDIR/${safe}-${kind}.log"
        ( cd "$cwd" && "$@" ) >"$logfile" 2>&1
        rc=$?
    else
        ( cd "$cwd" && "$@" )
        rc=$?
    fi
    end=$(date +%s%N)
    ms=$(( (end - start) / 1000000 ))

    if (( rc == 0 )); then
        printf '%-10s %-14s %s\n' "[ok]" "$module" "$desc"
        record "$kind" "$module" OK "$ms" "$logfile"
    else
        printf '%-10s %-14s %s (rc=%d)\n' "[FAIL]" "$module" "$desc" "$rc" >&2
        record "$kind" "$module" FAIL "$ms" "$logfile"
        FAILED=1
        if (( ! KEEP_GOING )); then STOP=1; fi
        if (( QUIET )) && [ -f "$logfile" ]; then
            printf -- '--- ultimas lineas de %s ---\n' "$logfile" >&2
            tail -n 40 "$logfile" >&2
        fi
    fi
}

skip_step() {
    local kind=$1 module=$2 reason=$3
    if (( DRY_RUN )); then
        printf '%-10s %-14s SKIP (%s)\n' "[$kind]" "$module" "$reason"
        return 0
    fi
    (( STOP )) && return 0
    printf '%-10s %-14s %s\n' "[skip]" "$module" "$reason"
    record "$kind" "$module" SKIP 0 ""
}

fail_step() {
    local kind=$1 module=$2 reason=$3
    if (( DRY_RUN )); then
        printf '%-10s %-14s FAIL (%s)\n' "[$kind]" "$module" "$reason"
        return 0
    fi
    (( STOP )) && return 0
    printf '%-10s %-14s %s\n' "[FAIL]" "$module" "$reason" >&2
    record "$kind" "$module" FAIL 0 ""
    FAILED=1
    if (( ! KEEP_GOING )); then STOP=1; fi
}

# -----------------------------------------------------------------------------
# Fases
# -----------------------------------------------------------------------------
phase_build() {
    local mod dir
    for mod in "${SELECTED[@]}"; do
        (( STOP )) && break
        dir=${MOD_DIR[$mod]}
        if (( CLEAN )); then
            emit_step clean "$mod" "make -C $dir clean GUI_TOOLKIT=gtk4" "$ROOT" \
                make -C "$ROOT/$dir" clean GUI_TOOLKIT=gtk4
        fi
        (( STOP )) && break
        emit_step build "$mod" "make -C $dir -j$JOBS all GUI_TOOLKIT=gtk4" "$ROOT" \
            make -C "$ROOT/$dir" -j"$JOBS" all GUI_TOOLKIT=gtk4
    done
}

phase_check() {
    local mod dir target
    for mod in "${SELECTED[@]}"; do
        (( STOP )) && break
        dir=${MOD_DIR[$mod]}
        if [ -z "${MOD_CHECK[$mod]}" ]; then
            skip_step check "$mod" "sin target check/test"
            continue
        fi
        for target in ${MOD_CHECK[$mod]}; do
            (( STOP )) && break
            emit_step check "$mod" "make -C $dir $target GUI_TOOLKIT=gtk4" "$ROOT" \
                make -C "$ROOT/$dir" "$target" GUI_TOOLKIT=gtk4
        done
        if (( SLOW )) && [ "$mod" = csnwphase ]; then
            for target in check-engine check-corpus; do
                (( STOP )) && break
                emit_step check "$mod" "make -C $dir $target GUI_TOOLKIT=gtk4" "$ROOT" \
                    make -C "$ROOT/$dir" "$target" GUI_TOOLKIT=gtk4
            done
        fi
    done
}

phase_test() {
    local mod dir off
    local live_ok=0
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        live_ok=1
    fi

    for mod in "${SELECTED[@]}"; do
        (( STOP )) && break
        dir=${MOD_DIR[$mod]}

        if [ "$mod" = hypodb ]; then
            if (( WITH_DB )); then
                emit_step test "$mod" \
                    "make -C $dir check GUI_TOOLKIT=gtk4 (EWGUI_DB_TEST_ENV=1)" "$ROOT" \
                    env EWGUI_DB_TEST_ENV=1 make -C "$ROOT/$dir" check GUI_TOOLKIT=gtk4
            else
                skip_step test "$mod" "requiere --db (PostgreSQL, ./ew8_db.sh up)"
            fi
            continue
        fi

        off=${MOD_OFFLINE[$mod]}
        if [ -n "$off" ]; then
            emit_step test "$mod" "make -C $dir $off GUI_TOOLKIT=gtk4" "$ROOT" \
                make -C "$ROOT/$dir" "$off" GUI_TOOLKIT=gtk4
        fi

        if (( LIVE )) && { [ "$mod" = csnhypodbp ] || [ "$mod" = ew_controller ]; }; then
            if (( live_ok )); then
                emit_step test "$mod" "make -C $dir test-live GUI_TOOLKIT=gtk4" "$ROOT" \
                    make -C "$ROOT/$dir" test-live GUI_TOOLKIT=gtk4
            else
                skip_step test "$mod" "--live sin DISPLAY/WAYLAND_DISPLAY"
            fi
            continue
        fi

        if [ -z "$off" ]; then
            skip_step test "$mod" "sin target check-offline"
        fi
    done
}

# Comandos de validacion de un modulo: lineas "binario|argumentos".
validate_cmds() {
    case "$1" in
        csnhypodbp)
            printf '%s\n' \
                'csnhypodbp|--print-config csnhypodbp.d' \
                'csnhypodbp|--check-profiles csnhypodbp.d'
            ;;
        csnrv)
            printf '%s\n' \
                'csnrv|--print-config csnrv.d' \
                'csnrv|--check-overlays csnrv.d'
            ;;
        csnstaevdisp)
            printf '%s\n' 'csnstaevdisp|--check-overlays csnstaevdisp.d'
            ;;
        csnwphase)
            # OJO: csnwphase lleva el subcomando DESPUES del .d.
            printf '%s\n' 'csnwphase|csnwphase.d --print-config'
            ;;
        *)
            :
            ;;
    esac
}

phase_validate() {
    local mod line bin args d binpath a
    local saved_keep_going
    local -a argv

    # El inventario de instalados se reporta, pero NO corta la validacion: con
    # --keep-going temporal los modulos seleccionados se validan igualmente y
    # el fallo queda contado en el resumen (exit 1).
    saved_keep_going=$KEEP_GOING
    KEEP_GOING=1
    emit_step validate "(todos)" \
        "sh ew_gui_tools/common/tools/check_install.sh -v" "$ROOT" \
        sh "$ROOT/ew_gui_tools/common/tools/check_install.sh" -v
    KEEP_GOING=$saved_keep_going

    for mod in "${SELECTED[@]}"; do
        (( STOP )) && break
        local lines
        lines=$(validate_cmds "$mod")
        if [ -z "$lines" ]; then
            skip_step validate "$mod" "sin flags de validacion"
            continue
        fi
        while IFS= read -r line; do
            (( STOP )) && break
            [ -n "$line" ] || continue
            bin=${line%%|*}
            args=${line#*|}

            d=""
            for a in $args; do
                case "$a" in
                    *.d) d=$a ;;
                esac
            done

            read -r -a argv <<< "$args"
            binpath="${EW_HOME:-$ROOT}/${EW_VERSION:-ew_8.0}/bin/$bin"
            if (( ! DRY_RUN )); then
                if [ ! -x "$binpath" ]; then
                    fail_step validate "$mod" \
                        "binario no instalado: ${binpath#$ROOT/} (compila con './ew8_compile.sh $mod')"
                    continue
                fi
                if [ -n "$d" ] && [ ! -f "$PARAMS_DIR/$d" ]; then
                    fail_step validate "$mod" "falta run_working_v8/params/$d"
                    continue
                fi
            fi
            emit_step validate "$mod" \
                "$bin $args  [cwd=run_working_v8/params]" "$PARAMS_DIR" \
                "$binpath" "${argv[@]}"
        done <<< "$lines"
    done
}

# -----------------------------------------------------------------------------
# Resumen
# -----------------------------------------------------------------------------
summary() {
    (( DRY_RUN )) && return 0
    local n=${#RES_KIND[@]}
    local ok=0 fail=0 skip=0 ms i

    if (( n == 0 )); then
        return 0
    fi

    echo ""
    echo "== resumen =="
    for i in "${!RES_KIND[@]}"; do
        case "${RES_STATUS[$i]}" in
            OK) ok=$(( ok + 1 )) ;;
            FAIL) fail=$(( fail + 1 )) ;;
            SKIP) skip=$(( skip + 1 )) ;;
        esac
        ms=${RES_MS[$i]}
        if [ "${RES_STATUS[$i]}" = SKIP ]; then
            printf '%-5s %-9s %-15s %s\n' \
                "${RES_STATUS[$i]}" "${RES_KIND[$i]}" "${RES_NAME[$i]}" "-"
        else
            printf '%-5s %-9s %-15s %d.%02ds\n' \
                "${RES_STATUS[$i]}" "${RES_KIND[$i]}" "${RES_NAME[$i]}" \
                $(( ms / 1000 )) $(( (ms % 1000) / 10 ))
        fi
    done

    echo ""
    if (( fail > 0 )); then
        echo "RESULTADO: FALLARON $fail paso(s)  (OK=$ok SKIP=$skip)"
        if [ -n "$LOGDIR" ] && [ -d "$LOGDIR" ]; then
            echo "logs: $LOGDIR"
        fi
        return 1
    fi
    echo "RESULTADO: OK  (OK=$ok SKIP=$skip)"
    return 0
}

# -----------------------------------------------------------------------------
# Listado de capacidades
# -----------------------------------------------------------------------------
list_modules() {
    local mod check test validate
    printf '%-15s %-28s %-46s %-24s %-22s %s\n' \
        "MODULO" "DIRECTORIO" "BINARIOS" "--check" "--test" "--validate"
    printf '%-15s %-28s %-46s %-24s %-22s %s\n' \
        "------" "----------" "--------" "-------" "-----" "---------"
    for mod in "${MODULES[@]}"; do
        check=${MOD_CHECK[$mod]:-}
        [ -n "$check" ] || check="-"
        if [ "$mod" = hypodb ]; then
            test="--db -> check"
        else
            test=${MOD_OFFLINE[$mod]:-}
            [ -n "$test" ] || test="-"
        fi
        validate=$(validate_cmds "$mod" | sed 's/.*|//' | tr '\n' ',' | sed 's/,$//')
        [ -n "$validate" ] || validate="-"
        printf '%-15s %-28s %-46s %-24s %-22s %s\n' \
            "$mod" "${MOD_DIR[$mod]}" "${MOD_BINS[$mod]}" \
            "$check" "$test" "$validate"
    done
    echo ""
    echo "grupos: all | libs | headless | gui"
    echo "verbos: --check (headless)  --test (offline)  --validate (configuracion)"
    echo "extras: --slow (csnwphase)  --live (GUI)  --db (hypodb)"
}

# -----------------------------------------------------------------------------
# main
# -----------------------------------------------------------------------------
main() {
    parse_args "$@"

    if (( SHOW_LIST )); then
        list_modules
        exit 0
    fi

    resolve_targets

    if (( ! DRY_RUN )); then
        ensure_env
        need_build_tools
    fi

    if (( QUIET && ! DRY_RUN )); then
        LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/ew8_compile.XXXXXX")
    fi

    (( DO_BUILD )) && phase_build
    (( DO_CHECK )) && phase_check
    (( DO_TEST )) && phase_test
    (( DO_VALIDATE )) && phase_validate

    summary
    local rc=$?
    exit "$rc"
}

main "$@"
