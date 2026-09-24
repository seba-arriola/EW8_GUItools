#-----------------------------------------------------------------------------
#Earthworm Common Variables - Chile EW8
#-----------------------------------------------------------------------------
#Este archivo define valores globales que pueden ser usados en cualquier .d
#usando la sintaxis: ${NOMBRE_VARIABLE}
SetEnvVariable INSTALLATION_ID       INST_UNKNOWN
SetEnvVariable EWMOLE_INSTANCENAME   EW8_CHILE_SERVER
SetEnvVariable EW_INSTALLATION       INST_UNKNOWN
#Configuracion para RabbitMQ (GLASS3 la necesitara pronto)
SetEnvVariable RABBITMQ_HOSTNAME     localhost
SetEnvVariable RABBITMQ_PORT         5672
SetEnvVariable RABBITMQ_EXCHANGE     earthworm_exchange
SetEnvVariable RABBITMQ_VIRTUALHOST  /
SetEnvVariable RABBITMQUSER          guest
SetEnvVariable RABBITMQPASS          guest
#Parametros para la API web (opcional)
SetEnvVariable EW2OPENAPI_APIBASEURL http://localhost:8080/api/
