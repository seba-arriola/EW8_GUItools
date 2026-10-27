# Pipeline de localización automática

Este documento fija los criterios de asociación y de identidad de evento que
usan `csnloc`, `hyp2000_ring` y `csnmags`. La configuración vive en
`run_working_v8/params/`; la tabla de locators, solo en `csnhypodbpqt.d`.

## 1. `csnloc`: gatillo, una solución por evento

`csnloc` es **asociador**. Emite una primera solución por evento a `HYPO_RING`
(versión 1) y no vuelve a emitir para ese evento. El ancla de identidad es la
**primera** solución, congelada:

```
ancla = (t0, lat, lon) de la primera solución
```

Un candidato es el mismo evento si:

```
d    = distancia_esferica(candidato, ancla)
tmax = EventMatchTimeSec + (EventMatchVelocityKmS > 0 ? d/EventMatchVelocityKmS : 0)
match = (|t0_candidato - t0_ancla| <= tmax) && (d <= EventMatchDistKm)
```

- `match`: no se emite, no se versiona, no se refina. Se actualiza el TTL.
- no `match`: evento nuevo (`version=1`). Si comparte picadas con un evento
  anterior se registra `overlap=k` (con `Debug>=1`), pero **no se fusiona**: el
  caso "comparte una picada pero es muy distinto" se trata como un gatillo nuevo
  (probablemente uno de los dos está mal).
- `EventPickTolSec` define cuándo una picada es "la misma": misma estación,
  misma fase y `|Δt| <= EventPickTolSec` (no compara red/canal/loc).
- El TTL (`EventTTLSec`) se mide con **wall-clock** desde la última detección,
  no desde el `t0`.

Puertas del gatillo (0 = off):

| Clave | Efecto |
|---|---|
| `MinPPhases` | P mínimas del candidato. `1` evita gatillar solo con S (telesismos, ruido) |
| `MaxAssocDistKm` | no usar estaciones más lejanas que esto para el gatillo |
| `MaxGapDeg` | no gatillar con gap azimutal peor |

Estas puertas son **solo** del gatillo: los locators reciben después todas las
P/S que asocien por tiempo de viaje.

## 2. Locators vivos

`hyp2000_ring` con `live_usage=1`:

1. Toma el hipocentro del seed (`HYPO_RING` o `HYPO_RING_REF2` confirmada).
2. Para cada P/S de `PICK_RING` predice `t0 + tt(seed, estación, fase)` y la
   acepta si `|t_pick - t_pred| <= LivePickResidualSecP/S`. La ventana crece con
   la distancia y la profundidad; no es una ventana fija alrededor de `t0`.
3. Construye el ARC **solo** con las picadas asociadas; no copia las fases
   viejas del seed.
4. Refina y valida `rms/nph/gap` de la salida. Si no pasa (`rms=99.99`, `nph` o
   `gap` fuera de rango), no publica, no actualiza el seed y cuenta el intento.
5. Publica en `HYPO_RING_REF`; tras `N_retry` intentos sin picadas nuevas o sin
   solución válida deja de localizar ese evento.

`nlloc_ring` sigue con `live_usage=0`: no procesa el anillo, se usa por el
`locate` manual/offline. Su salida también se valida (`rms/nph/gap`), pero el
modo vivo no está implementado.

## 3. Magnitudes

`csnmags` procesa todas las líneas de fase del ARC. El ancla de ML/Mb/Mwp es la
**P** de cada estación; una estación solo-S no entra en esas magnitudes. El
`TYPE_MAGNITUDE` publica solo las estaciones P que sobrevivieron y el log de
`Debug` informa `sinP` y `ml_vertical` (cuántas estaciones ML usaron la vertical,
si `Ml_AllowVerticals` estuviera activo).

## 4. Referencias

- Glass3: `HypocenterTimeWindow 30 s`, `HypocenterDistanceWindow 3.0°`,
  `AssociationStandardDeviationCutoff 3.0`, `DistanceCutoffFactor 5.0`,
  `DistanceCutoffMinimum 30 km`; asociación por tablas de tiempo de viaje.
  <https://usgs.github.io/neic-glass3/GlassConfiguration.html>
- Earthworm binder: `t_dif -1..120 s`, `rAvg_Factor 10`, `taper`, `thresh 6`,
  `rstack 100 km`, `scavenging/culling`.
  <http://earthwormcentral.com/documentation/ovr/binder_setup.html>
- SeisComP `scevent`: dedup por 5° + 60 s **o** >=3 picadas comunes.
  <https://www.seiscomp.de/doc/apps/scevent.html>
- SeisComP `scautoloc`: `minPhaseCount 6`, `maxResidual 7 s`, `maxRMS 3.5 s`,
  `maxSGAP 360`.
  <https://docs.gempa.de/seiscomp/current/apps/scautoloc.html>
- SeisComP `scanloc`: clustering `d = sqrt(Δt² + (Δx/v)²)`, `maxDist 1000 km`,
  `minPhase 4`.
  <https://docs.gempa.de/scanloc/current/apps/scanloc.html>
- PyOcto: `pick_match_tolerance 0.8 s`, `min_interevent_time 3 s`.
  <https://pyocto.readthedocs.io/en/latest/pages/associator.html>
