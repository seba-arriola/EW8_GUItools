# Barrido de parámetros de csnloc contra el catálogo publicado — resultados

El catálogo `tests_soluciones_publicadas.dat` es una **referencia para mirar a ojo**, no una
métrica. Los eventos son reales, así que **el set que más eventos del archivo encuentra (y más
cerca) es el que más le apunta**; las soluciones adicionales son un plus a revisar, no un defecto.

- **Captura**: `picks_ps/20261006-232343` (P+S, 408 tanks).
- **Catálogo**: 410 eventos.
- **Binario**: `csnloc` sha256 `2ff357a7…` (sin cambios durante el barrido).
- **Lectura**: la solución localizada más parecida a cada evento del archivo, contada a ≤25/≤50/≤100 km.
- **Reportes por set**: `loc_report.py` (parámetros + grillas + eventos) y `catalog_report.py`
  (cruce + adicionales + resumen). Ver `RUNBOOK.md` §10.

## Comparación de sets (ordenada por eventos a ≤100 km)

| set (overrides sobre `csnloc.d`) | con solución | ≤25 km | ≤50 km | ≤100 km | sin solución | mediana (km) | adicionales |
|---|---:|---:|---:|---:|---:|---:|---:|
| **assoc60** (`AssocWindowSec 60`) | 385 | **217** | **344** | **371** | **25** | **23.0** | 1085 |
| a60_eps90 (60 + `DBSCAN_Eps 90`) | 385 | 216 | 344 | 371 | 25 | 23.0 | 1067 |
| a180 (`AssocWindowSec 180`) | 380 | 164 | 296 | 345 | 30 | 28.3 | 644 |
| eps90 (`DBSCAN_Eps 90`) | 372 | 164 | 300 | 341 | 38 | 27.6 | 726 |
| **base** (`csnloc.d` actual, `AssocWindowSec 120`) | 372 | 164 | 299 | 340 | 38 | 27.6 | 742 |
| bp05 (`BackProjThreshold 0.5`) | 372 | 164 | 299 | 340 | 38 | 27.6 | 742 |
| refiter5 (`RefineIterations 5`) | 372 | 164 | 299 | 340 | 38 | 27.6 | 742 |
| refdepth40 (`RefineDepthKm 40`) | 372 | 164 | 297 | 340 | 38 | 27.6 | 733 |
| refdepth20 (`RefineDepthKm 20`) | 372 | 163 | 300 | 340 | 38 | 27.8 | 738 |
| ws10 (`PhaseWeightS 1.0`) | 372 | 173 | 301 | 339 | 38 | 27.5 | 732 |
| eps30 (`DBSCAN_Eps 30`) | 371 | 168 | 295 | 339 | 39 | 27.5 | 817 |
| refnode2 (`RefineNodeKm 2`) | 372 | 159 | 296 | 339 | 38 | 27.6 | 757 |
| minph4 (`MinPhasesPerEvent 4`) | 366 | 162 | 297 | 337 | 44 | 27.6 | 619 |
| assoc240 (`AssocWindowSec 240`) | 387 | 169 | 292 | 336 | 23 | 27.9 | 516 |
| a240_eps90 (240 + eps90) | 386 | 169 | 292 | 336 | 24 | 27.9 | 506 |
| a240_refd20 (240 + refdepth20) | 387 | 166 | 291 | 336 | 23 | 28.4 | 507 |
| a240_minph4 (240 + minph4) | 380 | 168 | 292 | 335 | 30 | 27.9 | 443 |
| a240_mp4_eps90 (240 + minph4 + eps90) | 380 | 168 | 292 | 335 | 30 | 27.9 | 435 |
| a240_mp4_refd20 (240 + minph4 + refdepth20) | 380 | 165 | 291 | 335 | 30 | 28.2 | 435 |
| prior60_40 (`DepthPriorKm 60` + `Sigma 40`) | 369 | 170 | 296 | 332 | 41 | 26.9 | 669 |
| prior30_20 (`DepthPriorKm 30` + `Sigma 20`) | 370 | 163 | 289 | 328 | 40 | 27.6 | 643 |
| a240_mp4_prior30 | 375 | 166 | 285 | 326 | 35 | 27.7 | 365 |
| a240_prior30 (240 + prior 30/20) | 382 | 166 | 284 | 325 | 28 | 28.0 | 427 |
| a300_mp4 (`AssocWindowSec 300`) | 378 | 155 | 270 | 318 | 32 | 29.7 | 400 |
| t0_4 (`T0ToleranceSec 4`) | 345 | 82 | 204 | 274 | 65 | 43.3 | 444 |
| maxrms1 (`MaxRMS 1`) | 298 | 94 | 181 | 226 | 112 | 35.6 | 288 |

## Refinamiento de la ventana (la palanca dominante)

| set | con solución | ≤25 km | ≤50 km | ≤100 km | sin solución | mediana (km) | adicionales | soluciones |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **assoc45** (`AssocWindowSec 45`) | 388 | 236 | 356 | **377** | **22** | **21.8** | 1389 | 1777 |
| assoc30 (`AssocWindowSec 30`) | 388 | **238** | 350 | 374 | 22 | **21.5** | 1669 | 2057 |
| assoc60 | 385 | 217 | 344 | 371 | 25 | 23.0 | 1085 | 1470 |
| assoc60_mp4 (60 + `MinPhasesPerEvent 4`) | 381 | 215 | 342 | 367 | 29 | 23.0 | **874** | 1255 |
| assoc90 (`AssocWindowSec 90`) | 368 | 177 | 310 | 349 | 42 | 25.7 | 884 | 1252 |
| base (120) | 372 | 164 | 299 | 340 | 38 | 27.6 | 742 | 1114 |

El óptimo está en **~45 s**: por debajo (30 s) aparecen casi los mismos eventos pero se disparan
las soluciones (2057 vs 1777) y los adicionales (1669 vs 1389); por encima (60-120-180) aparecen
menos. `assoc90` es **peor** que el base: la relación no es monótona.

## Lectura

- **`AssocWindowSec` es la palanca dominante, y el óptimo está en ~45 s.** Bajarla de 120 a 45 hace
  aparecer **+37 eventos** del archivo a ≤100 km (340 → 377), **+72 a ≤25 km** (164 → 236), reduce
  los eventos sin ninguna solución (38 → 22) y baja la mediana del desvío (27.6 → 21.8 km). Subirla
  a 180-240 reduce los adicionales pero también los eventos encontrados.
- **`DBSCAN_Eps 90`** apenas mueve la aguja (341 vs 340 a ≤100 km).
- **Palancas muertas**: `RefineIterations 3→5` y `BackProjThreshold 0.5` dan resultados
  **idénticos** al base (0.5 ≡ automático; el refinamiento ya converge en 3 iteraciones).
- **Dañinas**: `T0ToleranceSec 4` (274 a ≤100 km, 65 sin solución) y `MaxRMS 1` (226, 112).
- **Priors de profundidad**: no ayudan a "aparecer" (325-332) y suben ligeramente la mediana.
- **`MinPhasesPerEvent 4`**: sube los "sin solución" (38 → 44) y baja algo los encontrados.
- Los **adicionales** son el precio de la ventana corta (1085 vs 742): a revisar, no un defecto.

## Coste operativo de `AssocWindowSec 60`

En tiempo real el asentamiento es `AssocWindowSec/2 + 1` s: ~31 s con 60 frente a ~61 s con 120.
Baja la latencia de emisión.

## Recomendación

**`AssocWindowSec 45`** es el set que más le apunta: 377/410 eventos del archivo a ≤100 km (base
340), 236 a ≤25 km (base 164), 22 sin solución (base 38), mediana 21.8 km (base 27.6).

Precio: las soluciones suben de 1114 a 1777 y los adicionales de 742 a 1389 (a revisar). Si el
volumen de adicionales importa, **`AssocWindowSec 60` + `MinPhasesPerEvent 4`** da 367 a ≤100 km
con 874 adicionales (vs 742 del base): casi el mismo "aparecen" con muchos menos extras.

## Pendiente

- Fase 3: encadenar `hyp2000_ring`/`nlloc_ring` sobre los ARC de `csnloc`.

---

## 8. Claves ocultas de `csnloc.d` (18 variantes, captura completa)

De las **9 claves que existen en el codigo y NO estan en el `.d`**, 16 de 18 variantes dan
resultados **identicos al base**: las tolerancias de asociacion (`PhaseAssocTolSec/S`), los
residuales maximos (`PhaseResidualMaxSec/S`), la degradacion por RMS/gap (`MaxRMSDegrade`,
`MaxGapDegradeDeg`) y el renucleo (`RenucleateMinNewPhases`) **no limitan nada** en esta captura.
La que manda es la deduplicacion de eventos:

| set | <=25km | <=50km | <=100km | mediana | adicionales | soluciones |
|---|---|---|---|---|---|---|
| base (`EventDedupKm 100`) | 236 | 356 | 377 | 21.8 | 1389 | 1777 |
| **`EventDedupKm 50`** | **258** | **368** | **379** | **20.4** | 1823 | 2211 |
| `EventDedupKm 200` | 234 | 352 | 374 | 22.0 | 1024 | 1412 |
| `EventDedupSec 15` | 236 | 356 | 377 | 21.8 | 1410 | 1798 |

Con 100 km se estaban **fusionando eventos cercanos**; a 50 km se recuperan 22 eventos a <=25 km y 12
a <=50 km, y la mediana baja 1.4 km. A 200 km se pierden. Es el tipo de efecto que solo aparece con
muchos eventos por ventana, o sea justo en el escenario de procesar miles de mSeed.

## 9. Grillas de nucleacion: tiling A vs produccion

Generador nuevo: `tank_tools/mk_csnloc_grids.py` (antes eran ficheros hechos a mano, una
transcripcion de la tiling de Glass3). Tiling **A**: se conservan las franjas de latitud de
produccion y la **longitud** de cada una pasa a ser la UNION con los bboxes del modelo 3D de Potin
que solapan esa latitud, mas `magallanes` extendida hasta -56.0; la regional pasa a 31 capas (antes
13) y se ensancha a -80/-64.

`--check` geometrico: **0 regresiones** contra las locales de produccion y **100 % del pie de Potin
cubierto**, con **+71 % de area cubierta por locales**.

Medido en el subconjunto `test1*` (110 tanks):

| set | con_sol | <=25km | <=50km | <=100km | mediana | soluciones |
|---|---|---|---|---|---|---|
| base (10 km, produccion) | 107 | **69** | **99** | 105 | 20.7 | 515 |
| **A (10 km, cajas A)** | 108 | 67 | 95 | 105 | 20.2 | 529 |
| A5 (5 km, cajas A) | 108 | 64 | 95 | 105 | 21.7 | 523 |

- **A encuentra mas eventos** (366 vs 349 en el tank-a-tank) y los exclusivos suyos estan en las zonas
  que A cubre y produccion no: lon -64.9, -66.1, -65.8, -68.2 (Argentina/Bolivia) y offshore (-71.1,
  -74.5, -75.3). El precio es 2-4 eventos menos dentro de 25/50 km (los que encuentra de mas caen
  fuera de Chile, asi que no casan con el catalogo chileno) y ~36 % mas de tiempo.
- **5 km es PEOR en todo** (<=25: 64 vs 67; mediana 21.7 vs 20.2) y cuesta ~2x. **Descartado.**
- A perturba ~7 % de las soluciones (`TEMPORAL_SIN_ESPACIO=25` de 349): **no es un no-op**.
- El mes de test no alcanza para decidir: A gana en cobertura (verificable sin sismos) y pierde 2-4
  eventos en el recuento, pero los eventos que A agrega son de la zona de frontera y offshore, que en
  un mes aparecen pocas veces.

### 9.1 Resultado a escala: la tiling A EMPEORA (corrida completa, 408 tanks)

| set | con_sol | <=25km | <=50km | <=100km | sin_sol | mediana | adicionales | soluciones |
|---|---|---|---|---|---|---|---|---|
| base (produccion, dedup 100) | 388 | 236 | 356 | 377 | 22 | 21.8 | 1389 | 1777 |
| **`g_d50` (solo `EventDedupKm 50`)** | 388 | **258** | **368** | **379** | 22 | **20.4** | 1812 | 2200 |
| `g_reg31` (solo la regional nueva) | 389 | 227 | 356 | 377 | 21 | 22.4 | 1416 | 1805 |
| `g_A` (tiling A) | 389 | 221 | 338 | 368 | 21 | 22.6 | 1467 | 1856 |
| `g_A_d50` (A + dedup 50) | 389 | 235 | 349 | 373 | 21 | 21.7 | 1849 | 2238 |

En el subconjunto `test1*` A parecia neutra; **a escala es claramente peor** (-15 a <=25 km, -18 a
<=50, -9 a <=100, mediana +0.8 km). Y `g_reg31` aisla la causa: solo cambiar la regional (13 -> 31
capas, mas ancha) ya cuesta -9. El dano no viene de las cajas, viene de **mas capas / mas area**.

**La leccion** (tres experimentos independientes apuntan al mismo lado):

| experimento | que agrega | efecto a <=25 km |
|---|---|---|
| `g_reg31` | 13 -> 31 capas de profundidad | -9 |
| `g_A` | cajas ensanchadas al pie de Potin | -15 |
| `g_A5` | 10 -> 5 km en las locales | -5 |

A produce MAS soluciones (1856 vs 1777) y MAS adicionales (1467 vs 1389) pero PEORES en el cruce. La
etapa de seleccion (dedup + "gana la grilla mas fina") no distingue los candidatos buenos de los
espurios, asi que mas cobertura **diluye**. Se ve en el propio dato: `g_A_d50` (235) es mejor que
`g_A` (221), o sea el dedup estaba interactuando.

**Conclusion**: "cubrir mas" no es gratis y por si solo empeora. El criterio geometrico (cubrir el
pie del modelo) se cumple al 100 % pero **no es suficiente**. Para ampliar cobertura hay que arreglar
antes la seleccion, y la via es `MinPhases`/`BackProjThreshold` **por grilla** (que `grid.c` lee y
produccion no fija): una caja nueva solo nuclea si hay evidencia suficiente y no compite con los
eventos chilenos. En evaluacion: `g_A_mp5`, `g_A_bp70`, `g_A_mp5bp70`, `g_cur_mp5`.

**Aplicado**: `EventDedupKm 50.0` en `run_working_v8/params/csnloc.d` (antes corria a default 100).

### 9.2 La seleccion por grilla tampoco: conclusion cerrada

Seis variantes mas, misma corrida (408 tanks, 388 eventos), con `MinPhases`/`BackProjThreshold`
**dentro de cada `.grid`** (claves que `grid.c` lee y que produccion no fija):

| set | con_sol | <=25km | <=50km | <=100km | mediana | adicionales | soluciones |
|---|---|---|---|---|---|---|---|
| **`g_d50` (baseline: produccion + dedup 50)** | 388 | **258** | **368** | **379** | **20.4** | 1823 | 2211 |
| `g_cur_mp5` (cajas actuales + `MinPhases 5`) | 388 | 252 | 365 | 378 | 20.7 | 1582 | 1970 |
| `g_A_d50` (A + dedup 50) | 389 | 236 | 347 | 373 | 21.7 | 1843 | 2232 |
| `g_A_bp70` (A + `BackProjThreshold 0.7`) | 389 | 231 | 349 | 373 | 21.8 | 1839 | 2228 |
| `g_A_mp5` (A + `MinPhases 5`) | 389 | 232 | 344 | 370 | 21.9 | 1581 | 1970 |
| `g_A_mp5bp70` (A + ambas) | 389 | 228 | 346 | 370 | 21.9 | 1580 | 1969 |

**Ninguna mejora el baseline.** `g_cur_mp5` aisla la estrictez con las cajas de produccion: pierde 6
eventos a <=25 km y baja el volumen de 2211 a 1970 soluciones. El tank-a-tank lo confirma: `g_d50`
encuentra **177 eventos que `g_cur_mp5` no**, y `g_cur_mp5` solo 44 que `g_d50` no. Es decir: los
candidatos que la estrictez elimina son **reales**.

**Conclusion cerrada**: en esta tuberia, **todo lo que cambie el conjunto de candidatos de nucleacion
empeora** — mas cobertura (A, reg31), menos (mp5, bp70) o mas fino (5 km). El unico eje que gana es
el **dedup** (`EventDedupKm 50`), que no toca los candidatos: deja de fusionar eventos distintos.
Las grillas de produccion estan, de hecho, en un **optimo local**; el hueco de cobertura de frontera
y fosa es real pero cerrarlo cuesta mas de lo que da mientras la etapa de seleccion (dedup + "gana la
grilla mas fina" + score) no distinga un candidato bueno de uno espurio. Eso seria un cambio de
algoritmo, no de parametros.

**Aplicado**: `EventDedupKm 50.0` en `csnloc.d`. Grillas y `NodeKm`: **sin cambios**.
