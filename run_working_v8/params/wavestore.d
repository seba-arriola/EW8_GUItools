# wavestore.d — ingester del store de formas de onda indexado
#
# `wstore_ingest` lee SLINK_RING con su PROPIO cursor (varios procesos pueden
# leer el mismo anillo: convive con csntvp) y escribe segmentos diarios por canal
# en StoreRoot. ES la fuente de ondas local: wave_serverV ya no se despliega.
#
# Arranque (ya esta en startstop_unix.d y startstop_replay.d):
#     Process       "wstore_ingest wavestore.d"
#     Class/Priority    OTHER 0
#
# Para cargar tanks historicos al store (offline/calibracion):
#     ew_gui_tools/wavestore/tank2store <master.tank> --root <StoreRoot>

MyModuleId    MOD_WSTORE
RingName      SLINK_RING
StoreRoot     ../wstore
RetentionDays 7
IngestShards  4
HeartbeatInt  30
LogFile       1
