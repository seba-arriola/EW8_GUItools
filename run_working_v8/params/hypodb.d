# hypodb_ingest — escribe la información hipocentral de los anillos en
# PostgreSQL (event/origin/pick/magnitude/station_magnitude). Ver db/README.md
# y ew_gui_tools/hypodb/. Credenciales en db.env (junto a este archivo).
MyModuleId MOD_HYPODB
InRing HYPO_RING
InRingRef HYPO_RING_REF
InPickRing PICK_RING
DbEnvFile db.env
GroupPickTolSec 0.1
GroupMinMatchPicks 3
EventLoadHours 24
PickMatchTolSec 0.05
HeartBeatInt 30
LogFile 1
Debug 1
