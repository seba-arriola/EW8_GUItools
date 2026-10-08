# Inventario del espacio de parámetros de los tres localizadores

Este documento es el mapa de **qué se puede tocar** en `csnloc`, `hyp2000_ring` y `nlloc_ring`, con
el valor de producción, si se barrió y con qué veredicto. Sin esto, cualquier conclusión sobre
parámetros es incompleta por construcción: la mitad de las palancas no está en los ficheros de
configuración.

Leyenda: **barrido** = se probaron valores y hay resultado medido · **no** = nunca se tocó ·
**(oculto)** = la clave existe en el código pero **no está en el `.d` de producción**, así que
corre a default y nadie la ve.

---

## 1. `csnloc` — 44 claves leídas, 12 barridas

Fuente: `ew_gui_tools/csnloc/csnloc.c:117-166` (lectura), `:37-86` (defaults), `csnloc.h:53-113`.
Barrido: `tank_tools/calib/csnloc_variants.json` (30 sets). Resultados: `calib/RESULTS.md`.

### 1.1 Barridas

| clave | default | producción | probado | veredicto |
|---|---|---|---|---|
| `AssocWindowSec` | 120.0 | **45.0** | 30/45/60/90/180/240/300 | **palanca dominante**; óptimo 45 |
| `MinPhasesPerEvent` | 3 | 3 | 4 (7 sets) | sube los sin-solución (38→44); no ayuda |
| `DBSCAN_Eps` | 60.0 | 60.0 | 30/90 (5 sets) | apenas mueve |
| `RefineDepthKm` | 2·`RefineNodeKm`=10.0 | 10.0 | 20/40 (4 sets) | neutro |
| `RefineNodeKm` | 5.0 | 5.0 | 2.0 (1) | neutro |
| `RefineIterations` | 3 | 3 | 5 (1) | **muerto** (idéntico al base) |
| `DepthPriorKm` + `DepthPriorSigmaKm` | 0.0 / 0.0 | 0.0 / 0.0 | 30/20, 60/40 (4) | no ayuda a aparecer |
| `T0ToleranceSec` | 2.0 | 2.0 | 4.0 (1) | dañino (345 vs 372 con solución) |
| `MaxRMS` | 2.0 | 2.0 | 1.0 (1) | dañino (298 vs 372) |
| `PhaseWeightS` | 0.8 | 0.8 | 1.0 (1) | neutro |
| `BackProjThreshold` | 0.0 (auto) | 0.0 | 0.5 (1) | **muerto** |

### 1.2 No barridas y OCULTAS (existen en el código, no en el `.d`)

Estas son el corazón del asociador y corren a default sin que nadie las vea:

| clave | default | qué controla |
|---|---|---|
| `PhaseAssocTolSec` | 2.0 | tolerancia de asociación de P (s) |
| `PhaseAssocTolSecS` | 4.0 | tolerancia de asociación de S (s) |
| `PhaseResidualMaxSec` | 3.0 | residual máximo de P para mantener la fase |
| `PhaseResidualMaxSecS` | 5.0 | residual máximo de S |
| `MaxRMSDegrade` | 0.10 | cuánto puede degradarse el RMS al renuclear |
| `MaxGapDegradeDeg` | 10.0 | ídem con el gap |
| `EventDedupSec` | 30.0 | deduplicación de eventos por tiempo; **barrida**: 30 ≡ 60 (inerte — manda `EventDedupKm`) |
| `EventDedupKm` | 100.0 | ídem por distancia |
| `RenucleateMinNewPhases` | 3 | fases nuevas para renuclear |
| `NumThreads` | 8 | hilos de la retroproyección; **barrida**: 1/4/8/16 = 152/86/71/65 s (techo 2.34× — `RefineHypo`/DBSCAN son seriales, ver §9 de `RESULTS_REFINERS.md`) |

### 1.3 No barridas y visibles

`StaFile` (`estaciones_107.txt`), `TauTable` (`iasp91`), `RePickWindowSec` (10.0),
`PickTTLSec` (300.0), `DBSCAN_MinPts` (3, marcado "reservado" en el README),
`PhaseWeightP` (1.0), `EventTTLSec` (300.0), `AgencyID`, `Author`.

Barridas con resultado **inerte**: `GridActivationMarginDeg` (1.0 ≡ 1.5) y `EventDedupSec`
(30 ≡ 60). En el barrido del plan (pendientes de cerrar): `GridActivationMinPicks` (`gmp2/5/8`) y
`MaxEventsPerWindow` (`mew2/10`).

### 1.4 Las GRILLAS — generador nuevo, primera tiling alternativa

Las **12 grillas referenciadas** (`global.grid` 100 km; `chile_regional.grid` 25 km con 13 capas;
10 locales de 10 km con 31 capas) definen la **nucleación por retroproyección**: el primer paso de
la localización, antes del refinamiento. `legacy_chile.grid` existe pero **no está referenciada**
(config de regresión). El barrido de `csnloc` sólo había movido `RefineNodeKm`/`RefineDepthKm`, que
son la grilla de *refinamiento*: la de nucleación **nunca se había tocado**.

Hasta ahora eran ficheros hechos a mano, una transcripción de la tiling de Glass3 (60×45 nodos a
10 km), sin generador ni forma de comparar alternativas. Ahora hay **`tank_tools/mk_csnloc_grids.py`**:

- materializa los `.grid` desde una tabla declarativa (`current`, `reg31`, `A`);
- `--check` calcula los nodos contra `MaxNodes` y la **cobertura geométrica** (celdas de 0.5° no
  cubiertas por ninguna local, y **regresiones** contra las locales de producción);
- `--node-km` / `--local-max-nodes` para las variantes de resolución.

**Acoplamiento que hay que respetar**: `MaxNodes` **no recorta, aborta el arranque**
(`grid.c:127-130` devuelve −2 y `csnloc.c:652-654` mata el módulo). Bajar `NodeKm` de 10 a 5
cuadruplica los nodos (una local pasa de ~85 k a ~330-760 k), así que hay que subir `MaxNodes` o el
módulo no arranca.

**Resultado: cerrado — ninguna variante mejora las grillas de producción.** Se probaron tres
direcciones y las tres empeoran (408 tanks, 388 eventos, baseline = producción + `EventDedupKm 50`
con **258 / 368 / 379, mediana 20.4**):

| dirección | set | ≤25 km | mediana |
|---|---|---:|---:|
| más cobertura | `A` (cajas ensanchadas al pie de Potin) | 236 | 21.7 |
| más capas/área | `reg31` (sólo la regional a 31 capas, −80/−64) | 227 | 22.4 |
| más fino | `A5` (5 km en vez de 10) | 64 (subconjunto) | 21.7 |
| más estricto | `MinPhases 5` por grilla en las actuales | 252 | 20.7 |
| más estricto | `BackProjThreshold 0.7` por grilla | 231 | 21.8 |

Y `g_cur_mp5` lo confirma evento a evento: el baseline encuentra **177 eventos que él no**, y él sólo
44 que el baseline no — o sea los candidatos que la estrictez elimina **son reales**.

**Conclusión**: en esta tubería, *cualquier* cambio del conjunto de candidatos de nucleación empeora,
en las dos direcciones. Las grillas de producción están en un **óptimo local**. El único eje que gana
es el **dedup** (`EventDedupKm 50`), que no toca los candidatos: deja de fusionar eventos distintos.
Cerrar el hueco de frontera/fosa requiere cambiar la **selección** (el algoritmo, no el parámetro):
mientras el score + el dedup + "gana la grilla más fina" no distingan un candidato bueno de uno
espurio, más cobertura sólo diluye. Detalle: `RESULTS.md` §9.1-9.2.

---

## 2. `hyp2000_ring` — 12 claves `.d` + el `.hyp` de HYPOINVERSE

Fuente: `ew_gui_tools/hyp2000_ring/hyp2000_ring.c:49-61` (defaults), `:101-112` (lectura).
El módulo **fija por código** `PHS 'arcIn'`, `ARC 'arcOut'`, `SUM 'none'`, `COP 5`, `CAR 3`
(`hyp2000_ring.c:312`), así que no son palancas.

### 2.1 Claves del `.d`

| clave | producción | barrido | veredicto |
|---|---|---|---|
| `CommandFile` | `hyp2000_ring.hyp` | — | — |
| `WorkDir` | `tmp/hyp2000_ring` | — | — |
| `GridFile` | `grids/chile_regional.grid` | no | sólo filtra bbox; no se probó |
| `MinPhases` | 4 | 3/6 | 6 descarta 382 ARC y pierde 7 eventos |
| `MaxRMS` | 2.0 | 1.0 | 1.0 **destructivo** (1648 descartes) |
| `Debug`/`LogFile`/`SourceCode`/`HeartBeatInt`/`InRing`/`OutRing`/`MyModuleId` | — | — | operativas |

### 2.2 Comandos del `.hyp` (el espacio real)

| comando | producción | barrido | veredicto |
|---|---|---|---|
| `CRH` (modelo) | `CRH 1 'chile_1d.crh'` | husen / ak135 / bandas(6) | diferencia ≤0.9 km de mediana: **no discrimina** |
| `MUL` (modelo por región) | (no está) | `T 3` / `T 1` | **no cambia nada** |
| `NOD` (círculos) | (no está) | nunca | RAD1=450/DRAD=150 **sin justificación en el repo** |
| `ZTR` (prof. de prueba) | `10.0 F` | 0.0 / 5.0 / 20.0 | 0.0 peor; 5/20 ≈ 10 |
| `DAM` (amortiguamiento, 9 nº) | `7. 30. 0.5 0.9 0.012 0.02 0.6 50. 800.` | 3. / 15. / D2FAR 250 | **15. y D2FAR 250 son mejores** |
| `POS` (Vp/Vs) | (no está; default 1.75) | 1.65/1.70/1.75/1.80/1.85 | ver §2.4 |
| `LET` (selección de fases) | `5 2 3 0 0` | `5 2 3 2 2` | sin efecto |
| `H71` | `2 3 3` | nunca | semántica no documentada en el repo |
| `200` (modelo cortical) | `T 1900 0` | nunca | semántica no documentada |
| `WET` (pesos por calidad) | (no está) | nunca | el ARC de `csnloc` no trae calidad → probable no-op |
| `KPR` | (no está) | nunca | ídem |

### 2.3 La construcción de los modelos (nunca tocada)

`mk_crh_from_3d.py` resume el modelo 3D a 1D **promediando sólo Vp horizontalmente sobre una caja
lat/lon** (`:104-107`), en ≤20 capas hasta 300 km, **sin Vs ni densidad**. Los 6 modelos por banda
son medias de cajas de ~8°×6°, o sea ~900×660 km que incluyen océano, antearco, arco y tras-arco.
No hay modelo `.S.mod` en el repo: los tiempos S de NLLoc se derivan del modelo P con un Vp/Vs fijo.

**Esto es lo que explica que el banding no gane**, no la idea del banding: `hytra.for:48-103`
mezcla hasta **3 modelos con taper coseno** (no "gana el primero"), así que la regionalización
funciona — lo que falla es que cada modelo 1D es una media demasiado gruesa.

### 2.4 `POS` (Vp/Vs) — hallazgo de implementación

`POS` es válido (`hycmd.for:415-423`, default 1.75) pero **HYPOINVERSE lo consume al leer `CRH`**:
si se emite después del bloque de modelo no tiene ningún efecto (medido: 5 valores de 1.65 a 1.85
dieron resultados idénticos byte a byte). El arnés ahora lo emite antes del modelo.

---

## 3. `nlloc_ring` — 21 claves `.d` + 11 claves/plantilla × 6 bandas + el generador

Fuente: `ew_gui_tools/nlloc_ring/nlloc_ring.c:48-90` (defaults), `:111-158` (lectura).

### 3.1 Claves del `.d`

| clave | default | producción | barrido | veredicto |
|---|---|---|---|---|
| `MinPhases` | 0 | 4 | 3 | sin efecto medible |
| `MaxRMS` | 0.0 | 2.0 | 1.0 | **destructivo** |
| `ModelBand` (×7) | — | 6 bandas 3D 1.5 km + `CHILE_4k` (fusionado, 4 km) | sí/no (fallback 1D) | el 1D es **inservible** (27 de 388 eventos); `CHILE_4k` cubre 108 estaciones y entra sólo si ninguna banda fina cubre el evento entero |
| `LocGridKm` | 0.0 (off) | **600.0** | nuevo | re-centra el `LOCGRID` en el epicentro de entrada; sin esto el octree **no converge** sobre el volumen de todo Chile |
| `ModelFallback` | — | 1D | — | — |
| `ControlFile` / `TtimeRoot` | — | — | — | sólo con el fallback |
| `OutRoot` / `WorkDir` / `GridFile` | — | — | no | `GridFile` filtra bbox |
| `TransOrigin` / `LocGrid` | — | (no están) | no | **sólo aplican sin bandas** |
| `NllIntervalSec` | 300 | **60** | sí | rate-limit del anillo; además hay un guardia `dirty` que omite la corrida si el ARC guardado no cambió (salvo el campo `version`). La tasa de activación del guardia necesita prueba **con anillo** |
| `NllTTLSec` | 1800 | 1800 | no | olvido de eventos pendientes |
| `NllMaxPending` | 64 | 64 | no | cola de pendientes |

### 3.2 Claves de las plantillas de control (× 6 bandas)

| clave | valor | barrido | veredicto |
|---|---|---|---|
| `LOCSEARCH` | `OCT 96 48 6 0.05 50000 10000 4 0` | sólo los 2 primeros nº (200/100 y 48/24) | más profundo **peor**; menos = igual |
| `LOCMETH` | `EDT_OT_WT 9999.0 4 -1 -1 1.78 6 -1.0 1` | sólo el VpVs (6º campo) | 1.73 algo mejor, 1.85 peor (20 eventos) |
| `LOCGAU` | `0.2 0.0` | 0.5 | sin efecto |
| `LOCGRID` | por banda, 1.5 km | escala ×2 | **sin efecto ni en resultado ni en tiempo** |
| `LOCGAU2` | `0.01 0.05 2.0` | nunca | — |
| `LOCQUAL2ERR` | `0.1 0.5 1.0 2.0 99999.9` | nunca | mapea calidad de pick → error |
| `LOCPHASEID` (P y S) | `P P p G PN PG` / `S S s G SN SG` | nunca | — |
| `TRANS` | por banda (centro) | nunca | debe casar con la generación de grillas |
| `LOCFILES` (`iSwap`) | `... 0` | nunca | — |
| `LOCHYPOUT` | `SAVE_HYPOINVERSE_Y2000_ARC` | nunca | requisito del módulo |

### 3.3 El generador de grillas (nunca tocado)

`mk_nll_grids_3d.py`: `--margin 0.5` (grados para elegir estaciones), bboxes derivados de la
cabecera del modelo 3D, `GT_PLFD 1.0e-3` (finura del cálculo de tiempos), y las `LAYERS` del
fallback 1D (`mk_nll_grids.py:17-19`: `5.50/0.0, 6.10/10.0, 6.90/35.0, 8.00/55.0`).
**Ninguno se barrió**, y el fallback 1D es justamente el peor de todos.

### 3.4 La cobertura de estaciones (defecto encontrado y corregido)

Las grillas de tiempos de cada banda se generan **sólo con las estaciones dentro del bbox de la
banda ± 0.5°** (`mk_nll_grids_3d.py:158-160`). Como las bandas se solapan ~4°, un evento en el
borde sur de una banda puede tener **todas** sus estaciones en la banda vecina. Con la selección
antigua ("gana la primera banda que contiene el epicentro") eso daba 0 observaciones y el evento se
descartaba **en silencio** (`nlloc_ring.c` `return -1` sin log). Medido: 15 % de descarte en
serial. Corregido en `select_model` (ahora gana la banda que cubre más estaciones del evento).

Y el residuo (33 % de eventos con cobertura parcial) se cierra con **`CHILE_4k`**, el modelo
fusionado de todo Chile descrito en `RESULTS_REFINERS.md` §2.4, más el re-centrado del `LOCGRID`
(`LocGridKm`) que NLLoc necesita porque no acepta hipocentro semilla.

### 3.5 Lo que queda abierto

- **`LOCSEARCH`/`LOCMETH` completos** (los ~18 números): el coste es de **~8 s por evento** y ni
  `LOCGRID` ni `LOCSEARCH` lo mueven, así que la palanca está sin encontrar. Para miles de mSeed
  esto es el límite real de throughput.
- **Estaciones al sur de −48.9°** (Magallanes, Antártida) y las oceánicas lejanas (Isla de Pascua,
  −109°) quedan fuera del modelo de Potin: `CHILE_4k` no puede cubrirlas. Sería otro modelo.
- **`LOCQUAL2ERR`** (calidad de pick → error) y **`LOCGAU2`**: nunca tocados, y el primero es la vía
  natural para aprovechar la calidad que ya calcula `pickS`.
