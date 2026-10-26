# Configuración de modelos de velocidad y regiones para los refinadores

Este documento es la referencia para configurar **modelos de velocidad** y **regiones** en los
módulos de anillo de segunda etapa (`hyp2000_ring`, `nlloc_ring`). La idea es reutilizar el
esquema de las **grillas de `csnloc`** (`.grid`) como descriptor de región, y colgar de ahí el
modelo de velocidad que cada motor necesita.

> Estado: documento de configuración. Los módulos `hyp2000_ring` / `nlloc_ring` se implementan
> en las Fases 3 y 4. Aquí queda **cómo se configura cada motor**, verificado contra su
> documentación.

---

## 0. Esquema compartido: `GridFile` (reuso del `.grid` de `csnloc`)

Los módulos leen una grilla con el **mismo formato que `csnloc`** y de ahí obtienen la región
(bbox + paso + capas de profundidad). El resto del modelo se referencia aparte:

```
# hyp2000_ring.d / nlloc_ring.d
GridFile      params/grids/chile_regional.grid   # <- formato .grid de csnloc
ModelFile     models/chile_1d.nd                 # modelo de velocidad (formato del motor)
StaFile       estaciones_loc.txt                 # estaciones en formato del motor
WorkDir       tmp/<modulo>/
```

Claves de `.grid` que se aprovechan:

| Clave | Uso en el refinador |
|---|---|
| `LatMin`/`LatMax`/`LonMin`/`LonMax` | región (bbox) |
| `NodeKm` | resolución espacial de referencia |
| `DepthMin`/`DepthMax`/`DepthStep` o `DepthLayers` | rango/capas de profundidad |

**Mapeo por motor:**

| Motor | Geometría de región | Traducción desde `.grid` |
|---|---|---|
| NonLinLoc | **bbox** (`LOCGRID`) | directa (mismo concepto) |
| HYP2000 | **círculos** (`NOD`) | no directa: ver §2.3 |

Grillas de producción (`run_working_v8/params/grids/`): `global.grid` (100 km),
`chile_regional.grid` (25 km, `0..300/25`) y 10 locales (10 km, `5..300`). Para las pruebas se
usa **`chile_regional.grid`** (`-56..-16`, `-76..-66`).

---

## 1. Modelos de velocidad disponibles (Chile)

| Modelo | Cobertura | Tipo | Disponibilidad |
|---|---|---|---|
| **Husen et al. 1999** | Norte de Chile (CINCA/Antofagasta) | 1D (Vp/Vs por capas) | Tabla numérica publicada |
| **Graeber & Asch 1999** | 21–23°S | 1D | Descrito; tabla no extraída |
| **Bohm et al. 2002** | Andes 36–40°S | 1D | Descrito; tabla no extraída |
| **Ammirati et al. 2024** | Andes australes (48–52°S) | 1D | Citado; tabla no extraída |
| **VELEST Atacama (Münchmeyer 2025)** | Atacama | 1D VELEST | Zenodo `15083298`, **CC-BY-4.0** |
| **Tomografía Potin 2019** | 18–43°S | 3D (formato NLL) | Zenodo `13146424`, **CC-BY-4.0** |
| **ABYSS 2025** | Chile central | 3D (formato NLL) | Zenodo `15741952`, **CC-BY-4.0** |
| `iasp91` / `ak135` | global | 1D | En el repo / público |

**Estrategia Chile completo:** un **1D por segmento** (norte / centro / sur / austral) es la
práctica (CSN usa 1D regionales; los catálogos nuevos usan 3D al norte de 43°S + 1D austral al
sur). Para el primer test basta un 1D general; luego se afina por segmento.

**Implementado (modelo de B. Potin, `/mnt/d/nll`).** Son grillas NLLoc nativas
(`.mod.hdr/.buf`, tipo `SLOW_LEN`) en 6 bandas de latitud (`N18-26` … `N38-46`),
a 1.5/3/4 km, más grillas de tiempo por estación.

- **`nlloc_ring` (3D directo):** `mk_nll_grids_3d.py` regenera las grillas de tiempo
  para **nuestras** estaciones con `GTMODE GRID3D` a partir de los `.mod` de Potin, y
  escribe un control por banda con su `TRANS`/`LOCGRID`. `nlloc_ring.d` declara una
  `ModelBand` por banda; el módulo elige por el hipocentro de entrada y cae a
  `ModelFallback` (1D) fuera de cobertura.
- **`hyp2000_ring` (1D derivado):** HYPOINVERSE solo admite 1D por capas, así que
  `mk_crh_from_3d.py` resume cada banda promediando `Vp(z)` horizontalmente a un
  `.crh` (≤ 20 capas, `NLYR=20`). El `.hyp` carga las 6 y regionaliza con
  `MUL T 3` + `NOD` (modelo por epicentro).

Cobertura efectiva (bbox de cada banda, 1.5 km): lat −45.9…−18.0, lon −75.9…−64.5.
La banda N18-26 **no** cubre el offshore al norte (lon < −71.5); el fallback lo
absorbe. Resolución: 1.5 km (producción) y 4 km (respaldo; las bandas de 3 km no
traen `.mod`). Ver `docs/REFINADORES_RING.md` §"Modelos de velocidad".

**Licencias:** los modelos de Zenodo son CC-BY-4.0 (vendorizables con atribución). Las tablas de
papers sin licencia explícita se transcriben citando la fuente.

---

## 2. HYP2000 (`hyp2000_ring`)

El motor es HYPOINVERSE-2000 (Klein). Se enlaza como librería (`hypoinv_wrapper`); el módulo
habla por comandos y el intercambio de datos es por ficheros `arcIn`/`arcOut`.

### 2.1 Fichero de comandos `.hyp`

```
200 T 1900 0                 # formatos Y2K
LET 5 2 3 2 2                # matching SCNL (con location codes)
H71 2 3 3                    # usa el header del ARC como hint de hipocentro
STA 'estaciones_loc.sta'     # fichero de estaciones (formato HYPOINVERSE)
CRH 1 'models/chile_1d.crh'  # modelo 1D por capas
COP 5                        # ARC de entrada + shadows
CAR 3                        # ARC de salida + shadows
ZTR 10.0 F                   # profundidad de prueba; F = no fijar
LOC
PHS 'arcIn'                  # (el modulo lo sobreescribe por evento)
ARC 'arcOut'
SUM 'none'
```

### 2.2 Formatos de modelo 1D

| Comando | Tipo | Formato del fichero |
|---|---|---|
| `CRH` | capas planas | línea 1: nombre `A30`; luego pares `profundidad Vp` |
| `CRT` | capas (con topografía) | idem |
| `CRL` | gradiente lineal sobre halfspace | idem (referido a elevación) |
| `CRV` | gradiente sobre halfspace homogéneo | idem (referido al nivel del mar) |
| `CRE` | multicapa (vía `trvcon.f` de HYPOELLIPSE) | mismo formato que `CRH` |

Ejemplo `chile_1d.crh` (esqueleto; sustituir por el modelo real):
```
CHILE_1D
0.0   5.5
10.0  6.1
35.0  6.9
55.0  8.0
```
> V debe **crecer** con la profundidad; máximo ~20 capas + halfspace.

### 2.3 Regiones: círculos, no bbox

HYP2000 **no** tiene regiones rectangulares. Multi-modelo se hace con:

```
CRT 1 'models/norte.crt'
DEL 1 'models/norte.del'
MUL T 1                       # modelo 1 = default fuera de circulos
CRT 2 'models/centro.crt'
NOD 37 -33.0 -71.0 50 10 2    # nodo: r=50 km, transicion 10 km -> modelo 2
```
- `MUL T 1` es **obligatorio** (modelo por defecto).
- `NOD` define **dos círculos concéntricos** (radio interior + anillo de transición).
- `ALT` designa un modelo alterno para estaciones concretas (marcadas en el `.sta`).

**Desajuste clave:** tus grillas `csnloc` son **bbox**; HYP2000 usa **círculos**. Para Chile
completo hay que aproximar cada región con uno o varios `NOD` (círculo que cubra el bbox), o
usar un único modelo 1D nacional.

### 2.4 Estaciones

Fichero de ancho fijo (82 columnas): `site` (1–5), `net` (7–8), `comp` (10–13), lat (16–29),
lon (31–38), delays (50–60), calibración (75–80), `loc-code` (81–82). Un **único** `.sta` para
toda la red es viable.

---

## 3. NonLinLoc (`nlloc_ring`)

El motor es NLLoc (Lomax), enlazado como librería: `int NLLoc(pid, fn_control,
param_line_array, n_param, obs_line_array, n_obs, return_locations, ..., &loc_list_head)`.

### 3.1 Grillas de tiempos: `Vel2Grid` (una vez, offline)

`Vel2Grid` es un **binario** (no función); se corre para precalcular las grillas. Control:
```
CONTROL 0 0
TRANS SIMPLE 0.0 0.0        # o GLOBAL para 1D esferico
VGTYPE P
VGTYPE S
VGGRID 2 300 40 0.0 0.0 0.0 1.0 1.0 5.0 SLOW_LEN   # 2 -> 1D por capas
VGOUT  time/chile
LAYER  0.0   5.50 0.00  3.10 0.00  2.60 0.00
LAYER  10.0  6.10 0.00  3.50 0.00  2.80 0.00
LAYER  35.0  6.90 0.00  3.90 0.00  3.00 0.00
LAYER  55.0  8.00 0.00  4.50 0.00  3.30 0.00
```
- `LAYER depth VpTop VpGrad VsTop VsGrad rhoTop rhoGrad`, en orden creciente; la última capa
  llega a infinito.
- `VGGRID xNum yNum zNum xOrig yOrig zOrig dx dy dz gridType`; `xNum=2` para un modelo 1D.
- `VGOUT` escribe `<root>.<P|S>.mod.{hdr,buf}`.

`Grid2Time` (también binario) genera los tiempos por estación:
```
CONTROL 0 0
TRANS SIMPLE 0.0 0.0
GTFILES  time/chile  time/chile  P  0
GTFILES  time/chile  time/chile  S  0
GTMODE   GRID3D ANGLES_NO
GTSRCE  PB01 LATLON -20.1 -70.2 0.0 0.0
GTSRCE  ...
```
- `GTSRCE label LATLON lat lon z elev` (una por estación y fase).
- Las grillas **se reutilizan** entre eventos (NLLoc solo las lee).

### 3.2 Localización: control de `NLLoc`

```
CONTROL 1 12345
TRANS SIMPLE 0.0 0.0
LOCFILES obs.nll NLLOC_OBS  time/chile  out/loc  0
LOCSEARCH OCT 10 10 10 1 10000 1000 NO
LOCMETH EDT_OT_WT 200.0 4 60 4 -1 0.0 6 0.1
LOCGRID 100 100 40 -76.0 -56.0 0.0 0.2 0.2 5.0 PROB_DENSITY SAVE
LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC
```
- `LOCFILES obsFiles obsType ttimeRoot outRoot iSwap`; `obsType` = `NLLOC_OBS` (o `HYPO71`,
  `HYPOCENTER`).
- `LOCGRID` es un **bbox**: `xNum yNum zNum xOrig yOrig zOrig dx dy dz gridType saveFlag`.
  **Se traduce directo** de `LatMin/LonMax`… de tu `.grid`.
- `LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC` produce el ARC Y2K para reinyectar al anillo.
- NLLoc **no acepta hipocentro semilla**: la restricción regional se hace con `LOCGRID`
  (centrado/limitado por la solución de `csnloc`) y `LOCMETH maxDistStaGrid`.

### 3.3 Formato de observaciones `NLLOC_OBS`

```
STA  INS CMP ON  PHA FM  yyyymmdd  hhmm  ss.sss  GAU  ErrMag  coda  amp  per
PB01 ?   ?   ?   P   U   20260722  0203  24.611 GAU  2.0e-02 0.0   0.0  0.0
```
Idéntico al bloque `PHASE` del `.hyp`; el módulo lo construye desde las fases del ARC de
`csnloc`.

### 3.4 Regiones: bbox + control por región

NLL cubre un país largo con **varios control files / perfiles**, cada uno con su `LOCGRID` y
`ttimeRoot`, seleccionados por la localización previa (patrón SeisComP `RouterLocator`). El
`TRANS` del control debe ser **el mismo** con que se generaron las grillas.

---

### 3.5 Profundidad fija (`FixedDepthKm`) en los tres localizadores

La GUI (`csnhypodbp`, barra de *picking*: `depth [caja] [☑ Fix]`) pasa la profundidad fija por
corrida, en una **copia temporal** del `.d` (nunca en `params/`). La clave es `FixedDepthKm`; su
ausencia o un valor `<0` deja la profundidad libre. `0` es una profundidad válida.

| localizador | soporte nativo | implementación | resultado |
|---|---|---|---|
| `csnloc` | no (busca en `nz` capas + refine) | `Grid_ForceDepth` deja **un nodo z** antes del precomputo CSR; `RefineHypo` no mueve z | **exacta** (ARC en centésimas) |
| `hyp2000_ring` | sí (`ZTR <z> T` ⇒ `DEPFIXA`) | el módulo emite `ZTR %.3f T` **después** del `@CommandFile` | **exacta** |
| `nlloc_ring` | **no** (NLLoc siempre busca 3D) | `LOCGRID` con `nz=2`, `z0=FixedDepthKm−FixDepthSpacingKm/2`, `dz=esp` y `LOCSEARCH OCT … init_num_cells_z=1` | ventana de **±esp/2** (default ±0.05 km), no exacta |

Notas:

- En `nlloc_ring`, `FixDepthSpacingKm` (default `0.1` km) es el tamaño de la ventana; `<=0` se
  sustituye por `0.1` con aviso. La receta es la oficial de NonLinLoc (su FAQ dice que no se puede
  fijar a un solo valor porque el motor siempre hace búsqueda 3D); SeisComP lo emula igual con
  `fixedDepthGridSpacing = 0.1`.
- El motor de NLLoc ya acepta `numz==2` como “depth fixed” (`NLLocLib.c:812`, `LocSum.c:359`): un
  resultado en el borde de la ventana no se descarta.
- Si el refinador elegido es `hyp2000_ring` o `nlloc_ring`, la clave viaja **también** en la pasada
  previa de `csnloc` (arranque exacto para el refinador).
- Un binario viejo que ignore la clave devuelve otra profundidad; `csnhypodbp` avisa en el log si
  la diferencia supera 0.5 km.
- **Validación real** (ARC de `HYPO_RING`, 5 fases): `nlloc_ring` offline sin `FixedDepthKm` dejó
  `LOCGRID … 221` y devolvió z≈93.88 km; con `FixedDepthKm 33.0` dejó
  `LOCGRID … 2 … 32.9500 … 0.1000` + `LOCSEARCH OCT … 1` y devolvió z≈32.96 km. El ARC de vuelta
  lo parseó `csnhypodbp` (`depth=33.0`) y la cadena `locate_chain … --fix-depth 33.0` terminó con
  `rc=0`, conservando el `qid` de la solución base. El fixture sintético de
  `nlloc_ring/test/test_fixdepth_offline.sh` no localiza con el motor, por eso ese test automático
  valida el control; la corrida con el ARC real valida motor + mensaje de vuelta.

---

## 4. Ejemplo de prueba (Chile regional)

Base: `chile_regional.grid` (`-56..-16`, `-76..-66`, 25 km, `0..300/25`).

- **NonLinLoc**: `LOCGRID` derivado del bbox anterior + un 1D general (`iasp91`/`ak135`) o el
  VELEST de Atacama.
- **HYP2000**: un `CRH` con el 1D de prueba; `ZTR 10 F`; `COP 5`/`CAR 3`; `H71 2 3 3`.

Con esto se valida la **integración** (anillo → motor → anillo) sin pretender aún el modelo
definitivo.

---

## 5. Generador de tablas tau de `csnloc` (verificado)

El generador (Buland-Kennett) es el `makeTTTable` de las fuentes de EarthWorm 8 (no se
versiona). Secuencia:
`REMODL` → `REMODL.HED/.TBL` → `SETBRN` → `<modelo>.HED/.TBL`. Comando:
`cd Iasp91 && make remodl setbrn && ./remodl && ./setbrn`.

**El modelo está hardcodeado en Fortran**, no en un `.nd`/`.tvel`:
`makeTTTable/Iasp91/emiasp91.f` define discontinuidades (`emiasp91.f:12-13`), radios (`:46-48`)
y coeficientes polinómicos de densidad/Vp/Vs (`:49-66`); el nombre de salida sale de `modnam`
(`:14`, ruta absoluta histórica → hay que editarla). `iasp91_P.vel`/`iasp91_S.vel` son
**salida/documentación**, no entrada.

Para un modelo regional: editar (o crear un `emi*.f` con) las capas en `emiasp91.f` y
regenerar. Ojo: el código es **Solaris-specific** (`readme.txt`), puede requerir portar a
gfortran. TauP/ObsPy **no** generan `.tbl/.hed` (solo `.taup`/`.npz`/LOCSAT).

## 6. Pendientes de verificar

- `SAVE_HYPOINVERSE_Y2000_ARC` byte a byte frente a `TYPE_HYP2000ARC` de `csnloc`.
- Compatibilidad del ARC de `csnloc` como `PHS` de HYP2000 (o conversión en el módulo).
- Portabilidad a gfortran del generador tau (Solaris-specific).

---

## 7. Ajuste de `AssocWindowSec` (medido)

El catálogo `tests_soluciones_publicadas.dat` y los scripts que lo cruzaban (`loc_report.py`,
`catalog_report.py`, `calibrate_csnloc.py`) vivían en un arnés de archivos de trazas, hoy fuera del repo.
Lo que dejan medido (408 archivos de trazas, `csnloc` `2ff357a7`, P+S): con `AssocWindowSec 120` (base) aparecen
340/410 eventos a ≤100 km (164 a ≤25 km), 38 sin solución, mediana 27.6 km; con `AssocWindowSec 60`
aparecen 371/410 (217 a ≤25 km), 25 sin solución, mediana 23.0 km.
