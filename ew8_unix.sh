#!/bin/bash

# Obtener la ruta absoluta de la carpeta EW8_GUItools
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "==================================================="
echo "   Iniciando Earthworm 8 Portatil (64-bits)        "
echo "==================================================="

# 1. Configurar variables de entorno de Earthworm para esta terminal
export EW_INSTALLATION="INST_UNKNOWN"
export EW_HOME="${DIR}"
export EW_VERSION="earthworm_8.0"
export EW_PARAMS="${DIR}/run_working_v8/params"
export EW_LOG="${DIR}/run_working_v8/log"
export SYS_NAME=`hostname`

# 1b. GTK4 (WSLg): renderer y backend. FUENTE UNICA en ew8_gtk_env.sh, compartida
#     con el portable (deploy_portable.sh lo copia tal cual). Antes este bloque
#     vivia solo aqui y el deploy, que regenera este archivo, lo perdi­a en
#     silencio. Ver docs/GTK4-MIGRATION.md seccion 11.1 /
#     docs/adr-0001-render-backend.md.
if [ -r "${DIR}/ew8_gtk_env.sh" ]; then
    . "${DIR}/ew8_gtk_env.sh"
else
    echo "AVISO: falta ${DIR}/ew8_gtk_env.sh (entorno GTK4 no aplicado)" >&2
fi

# 2. Agregar la carpeta bin al PATH
export PATH="${DIR}/${EW_VERSION}/bin:$PATH"

# 2b. LEGACY: integracion GLASS3 (asociador sismico externo).
#     El camino activo es csnloc; estas variables solo las usan los scripts
#     legacy kafka_monitor.sh / glass_monitor.sh (ver AGENTS.md seccion 8).
export GLASS_HOME="${DIR}/glass3"
export GLASS_RUN_DIR="${DIR}/glass3/run_glass"
export GLASS_BROKER_APP="${DIR}/glass3/neic-glass3/dist/glass-broker-app/glass-broker-app"
export GLASS_LOG="${DIR}/glass3/run_glass/logs"

# 2c. LEGACY: Apache Kafka (KRaft local, datos dentro del proyecto).
export KAFKA_HOME="/opt/kafka"
export KAFKA_CONFIG="${DIR}/kafka/config/server.properties"
export KAFKA_DATA_DIR="${DIR}/kafka-data"
export KAFKA_LOG_DIR="${DIR}/kafka-logs"
export KAFKA_BOOTSTRAP="localhost:9092"
export KAFKA_INPUT_TOPIC="glass3_input_topic"
export KAFKA_OUTPUT_TOPIC="glass_locations"

echo "Entorno configurado:"
echo " - HOME: $EW_HOME"
echo " - Version: $EW_VERSION"
echo " - Parametros: $EW_PARAMS"
echo " - Logs: $EW_LOG"
echo " - Binarios: ${DIR}/${EW_VERSION}/bin"
echo " - GLASS3 (legacy): $GLASS_HOME"
echo " - Kafka  (legacy): $KAFKA_BOOTSTRAP (datos en $KAFKA_DATA_DIR)"
echo ""

# 3. Dar permisos de ejecucion 
chmod +x "${DIR}/${EW_VERSION}"/bin/* 2>/dev/null || true

# 4. Iniciar startstop desde el directorio base (para que las rutas relativas funcionen)
cd "${DIR}"
echo "Entorno listo. Puedes ejecutar startstop..."
