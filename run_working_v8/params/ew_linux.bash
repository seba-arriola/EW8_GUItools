# Create an Earthworm environment on Linux!
# This file should be sourced by a bourne shell wanting 
# to run or build an EARTHWORM system under Linux.

# Set environment variables describing your Earthworm directory/version
export EW_HOME=/opt/earthworm/
export EW_VERSION=earthworm_7.9
export SYS_NAME=`hostname`

# Set environment variables used by earthworm modules at run-time
# Path names must end with the slash "/"
export EW_INSTALLATION=INST_MEMPHIS
export EW_PARAMS=${EW_HOME}/memphis/params/
export EW_LOG=${EW_HOME}/memphis/log/
export EW_DATA_DIR=${EW_HOME}/memphis/data/

export PATH=${EW_HOME}/$EW_VERSION/bin\:$PATH
