#!/bin/bash

# Obtener la ruta absoluta de la carpeta EW8_GUItools
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "==================================================="
echo "   Iniciando Earthworm 8 Portatil (64-bits)        "
echo "==================================================="

# 1. Configurar variables de entorno de Earthworm para esta terminal
export EW_INSTALLATION="INST_UNKNOWN"
export EW_HOME="${DIR}"
export EW_VERSION="ew_8.0"
export EW_PARAMS="${DIR}/run_working_v8/params"
export EW_LOG="${DIR}/run_working_v8/log"
export SYS_NAME=`hostname`

# 1b. GTK4 (WSLg): renderer y backend. FUENTE UNICA en ew8_gtk_env.sh. No lo
#     escribas aqui: duplicado se perdia en silencio. Ver
#     docs/GTK4-MIGRATION.md seccion 11.1 / docs/adr-0001-render-backend.md.
if [ -r "${DIR}/ew8_gtk_env.sh" ]; then
    . "${DIR}/ew8_gtk_env.sh"
else
    echo "AVISO: falta ${DIR}/ew8_gtk_env.sh (entorno GTK4 no aplicado)" >&2
fi

# 2. PATH: primero el blindaje de coreutils, luego los binarios de EarthWorm.
#    En este entorno ~/seiscomp/bin queda ANTES de /usr/bin y su `timeout` NO es
#    el de GNU coreutils (rechaza `timeout <segundos> <comando>`). tools/gnubin
#    garantiza que `timeout` a secas resuelva a GNU; los scripts del repo usan
#    ademas la ruta absoluta '/usr/bin/timeout'. Ver AGENTS.md seccion 7.
export PATH="${DIR}/ew_gui_tools/common/tools/gnubin:${DIR}/${EW_VERSION}/bin:$PATH"

# Aviso (no aborta): si algo reordena el PATH despues, `timeout` volveria a ser
# el de SeisComP. Ver common/tests/test_timeout_gnu.sh.
if [ "$(command -v timeout 2>/dev/null)" != "${DIR}/ew_gui_tools/common/tools/gnubin/timeout" ]; then
    echo "AVISO: 'timeout' no resuelve al shim del repo; usa '/usr/bin/timeout' en scripts" >&2
fi

echo "Entorno configurado:"
echo " - HOME: $EW_HOME"
echo " - Version: $EW_VERSION"
echo " - Parametros: $EW_PARAMS"
echo " - Logs: $EW_LOG"
echo " - Binarios: ${DIR}/${EW_VERSION}/bin"
echo " - GNU timeout: ${DIR}/ew_gui_tools/common/tools/gnubin"
echo ""

# 3. Dar permisos de ejecucion 
chmod +x "${DIR}/${EW_VERSION}"/bin/* 2>/dev/null || true

# 4. Iniciar startstop desde el directorio base (para que las rutas relativas funcionen)
cd "${DIR}"
echo "Entorno listo. Puedes ejecutar startstop..."
