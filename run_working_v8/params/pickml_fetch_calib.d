# pickml_fetch_calib.d — arma el corpus de calibración de `pickml` (un evento
# por invocación) SIN tocar el SDS mientras el store tenga las ondas.
#
#   cd run_working_v8/params
#   pickml_fetch pickml_fetch_calib.d --event <event_id> --out ../pickml/calib/<event_id>
#
# Escribe en <out>: event.traces, picks_ref.csv, stations.sta, meta.json.
# `stations.sta` trae los canales EXACTOS del evento: usarlo como StaFile evita
# el desajuste histórico de HHZ/BHZ.

# Credenciales de la BD. db.env apunta a la instancia dev (5438); para calibrar
# contra el dump con picadas manuales hay que apuntarlo a 5435.
DbEnvFile    db.env

# Fuente de ondas: primero el store local (rápido), SDS como respaldo. Con
# eventos de los últimos 7 días (RetentionDays) el SDS no se toca.
StoreRoot    ../wstore
SdsRoot      /mnt/sds/archive

# 1 = sólo picadas manuales (la referencia "buena"); 0 = todas las del origin
# elegido. La BD dev no tiene manuales: 0 para el primer ajuste.
OnlyManual   0

# 0 = todas las estaciones del evento. El tope existe para acotar volumen.
MaxStations  0

# Ventana descargada alrededor de las picadas (no del origen).
PadPreSec    60.0
PadPostSec   120.0

Rate         100.0
