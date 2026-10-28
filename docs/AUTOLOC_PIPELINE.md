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

Un candidato es el mismo evento si **cualquiera** de los dos criterios se cumple:

```
d    = distancia_esferica(candidato, ancla)
tmax = EventMatchTimeSec + (EventMatchVelocityKmS > 0 ? d/EventMatchVelocityKmS : 0)

geometrico = (|t0_candidato - t0_ancla| <= tmax) && (d <= EventMatchDistKm)
por_picadas = shared(candidato, ancla) >= EventMatchSharedPicks

match = geometrico || por_picadas
```

`shared` cuenta picadas con la misma estación y fase y `|Δt| <= EventPickTolSec`
(no compara red/canal/loc).

El criterio de picadas es necesario porque el ancla es la **primera** detección,
que puede ser una hipótesis pobre (3-5 fases, gap grande) con el hipocentro
desplazado. Medido el 2026-10-27 12:24:38: dos hipótesis del mismo sismo
separadas **199 km** y con **8 picadas en común** producían dos orígenes de
`csnloc`. Es el mismo criterio de SeisComP `scevent` (≥ 3 picadas comunes).

- `match` geométrico: no se emite, no se refina. Se actualiza el TTL.
- `match` por picadas: ídem, y si el candidato es mejor se **re-ancla** el
  registro (no se emite).
- no `match`: evento nuevo. Si comparte 1-2 picadas con un evento anterior se
  registra `overlap=k` como diagnóstico.
- El TTL (`EventTTLSec`) se mide con **wall-clock** desde la última detección.

### Emisión retenida

Un evento se crea en el registro en cuanto dispara el gatillo, pero **no viaja
al anillo** hasta que la solución es publicable (`CSLoc_Emittable`: `nph >=
MinEmitPhases`, `rms <= MaxRMS`, `gap <= MaxGapDeg`). Así la primera —y única—
solución de `csnloc` es la buena, en vez de la primera detección pobre.

Si ya se emitió y llega un candidato del mismo evento con al menos
`EmitImproveMinPicks` fases más, se emite una corrección con el **mismo `qid`**
(es el único caso en que `csnloc` versiona). Para el asociador (`live_usage=0`)
los consumidores muestran una sola fila por `(qid, mod)`: la versión más alta.
Los refinadores con `live_usage=1` conservan cada iteración como fila propia
(`<event>:<mod>:<version>:I`).

Puertas del gatillo (0 = off):

| Clave | Efecto |
|---|---|
| `MinPPhases` | P mínimas del candidato. `1` evita gatillar solo con S (telesismos, ruido) |
| `MaxAssocDistKm` | no usar estaciones más lejanas que esto para el gatillo |
| `MaxGapDeg` | no gatillar con gap azimutal peor |

`MaxGapDeg` se fijó en **360** con una medición sobre 359 036 orígenes de la BD
(2026-10-27): el RMS mediano es 0.30 s tanto en 280-300 como en 300-360, y salta
a 0.82 s a partir de 360. En una red **lineal** como la chilena el gap de un
sismo offshore es 300-350 por geometría, no por mala calidad; con 300 se
descartaba el 12,4 % de las soluciones buenas de `csnloc` y el 12,8 % de las de
`hyp2000`. Del mismo dato sale `MinPhases 5`: con 4 fases y 4 incógnitas el
ajuste es exacto y el RMS no informa.

Estas puertas son **solo** del gatillo: los locators reciben después todas las
P/S que asocien por tiempo de viaje.

## 2. Locators vivos

`hyp2000_ring` con `live_usage=1`:

1. Toma el hipocentro del seed (`HYPO_RING` o `HYPO_RING_REF2` confirmada).
2. Para cada P/S de `PICK_RING` predice `t0 + tt(seed, estación, fase)` y la
   acepta si `|t_pick - t_pred| <= LivePickResidualSecP/S`. La ventana crece con
   la distancia y la profundidad; no es una ventana fija alrededor de `t0`.
3. Construye el ARC con **las fases del seed** (que `csnloc` ya asoció contra
   ese hipocentro) **más** las picadas asociadas, dedup por estación+fase. Las
   fases del seed son el núcleo coherente: sin ellas, una ventana de ±3 s sobre
   un seed pobre captura picadas de otros sismos.
4. Refina y valida `rms/nph/gap` de la salida. Si no pasa (`rms=99.99`, `nph<=0`
   o fuera de rango), **poda** el ARC de entrada con los residuales del ARC de
   salida (las fases que el motor no puede explicar se descartan) y reintenta,
   hasta `LiveMaxIter` veces. Si tras las iteraciones sigue inválida, no publica,
   no actualiza el seed y cuenta el intento.
5. Publica en `HYPO_RING_REF`; tras `N_retry` intentos sin picadas nuevas o sin
   solución válida deja de localizar ese evento, pero **se reabre** si llega una
   picada nueva y el sismo es más reciente que `LiveEventTTLSec`. `N_retry`
   corta el gasto de CPU; no sentencia el evento.

La poda es lo que separa "39 fases asociadas → RMS 14.5 → evento perdido" de
una solución publicable, y la reapertura evita que un evento que falló 5 veces
quede sin refinar para siempre aunque después lleguen 30 picadas suyas.

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
