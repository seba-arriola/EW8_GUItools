# Inversión de fase W (`csnwphase` + panel `Mww`)

`csnwphase` integra el motor **eost/wphase** (vendorizado, binarios C+Fortran ya en su
sitio) como un cálculo **on-demand** sobre una solución hipocentral: prepara la
entrada, lo ejecuta y expone el resultado (Mw, momento, mecanismo focal y centroide).

**No es un módulo de anillos**: no se declara en `startstop_unix.d` y nada lo dispara
solo. Se invoca desde la GUI (*Procesar → magnitudes → `Mww`*) o por CLI.

---

## 1. Reparto de responsabilidades

| quién | qué hace |
|---|---|
| **nosotros** (`ew_gui_tools/csnwphase`, headless) | ondas (store/SDS), respuesta (`SAC_PZs_*` desde los `.pz` del repo), `i_master` + `CMTSOLUTION`, `run.sh` auditable, parseo de la salida, `wphase.json`, tabla de parámetros |
| **el motor** (binarios de `eost/wphase`) | decimación, ventanas W, LUT de respuesta, deconvolución+filtrado, kernels, inversión iterativa y grid search del centroide |

El motor sólo lee **dos** variables de entorno, y ambas abortan con `exit(1)` si faltan:

- `WPHASE_HOME` (de ahí sale `$WPHASE_HOME/P_tt_JB`)
- `GF_PATH` (= `GfDir`)

`RDSEED`, `ALT_RESPONSE_FILE`, `GMT_BIN`, `HOME` y `DISPLAY` son variables **muertas**
en el código C del motor.

## 2. Configuración: `run_working_v8/params/csnwphase.d`

| clave | para qué |
|---|---|
| `WphaseHome`, `WphaseBin` | instalación y binarios del motor |
| `GfDir` | base de funciones de Green: **absoluta y fuera del repo** (~21 GB) |
| `StoreRoot`, `SdsRoot`, `SdsFallbackHours` | ondas: store indexado y SDS (un evento más viejo que las horas de fallback va directo al SDS) |
| `StaFile`, `ResponseDir`, `ResponsePattern`, `ResponseInNm` | metadata y respuestas (`.pz`) |
| `Dmin`, `Dmax`, `MinMagnitude` | cribado de estaciones y umbral de disparo (el `Dmin`/`Dmax` es editable en el panel `Mww`) |
| `WinPre`, `WinPost` | ventana de ondas alrededor del origen |
| `ScreenTh`, `ScreenNr` | screenings del motor: **un solo** `-th` y un `-nr` (`RUNA3r.csh` encadenaba `5 3 1`; aquí se pide **una sola corrida** con los dos flags juntos). Un `ScreenTh` con varios valores se acepta por compatibilidad: se usa el **primero** y se avisa por `stderr` |
| `GsTsMin`, `GsTsDt`, `GsTsNit`, `GsXyDx`, `GsXyNx`, `GsXyNopt`, `GsXyNit` | **pasada xy fina** del grid search del centroide (`wpinversion_gs`) |
| `GsTsMax` | tope del barrido de time-shift; **solo se usa con `GsTsAuto 0`** |
| `GsTsAuto` | `1` (default) = el tope del barrido ts sale de la magnitud como en el wrapper del CSN (`<=7` → 30 s, `<=8` → 100 s, `>8` → 168 s; `wp_grid_search.py:173-185`). Con un tope fijo de 30 s el barrido se quedaba corto para M>7 (la half duration de un 8.3 es 39.28 s) |
| `GsHdsafe` | `1` (default) = `-hdsafe` en `wpinversion_gs`, como la producción del CSN (`wphase_scripts.sh` → `wp_grid_search.py -s`): el ts search re-sintetiza el kernel con `hd=ts` en vez de desplazar la ventana |
| `GsOld` | `1` (default) = `-old` (también del wrapper, `wp_grid_search.py:298`): reusa `az/gcarc/p2p/avg/wgt` de la lista de 13 columnas `o_wpinversion`. El `run.sh` lo comprueba y aborta con mensaje propio si esa lista trae menos de 13 campos |
| `GsXyzDz`, `GsXyzMinz`, `GsXyzDx`, `GsXyzNx`, `GsXyzNopt`, `GsXyzNit` | **pasada 3D** (lat/lon/prof) del grid search: `-dz`/`-minz` y la salida `grid_search_xyz_out`. Son los `DDEP`/`MINDEP`/`XYZ_*` del wrapper del CSN |
| `CorpusMinNused` | umbral de `n_used` del corpus de validación (`tools/corpus_report.sh`): las corridas con menos trazas usadas no se juzgan contra el catálogo, se cuentan aparte. Default **7** |
| `GridSearch`, `TimeoutSec`, `KeepTmp`, `MaxConcurrent`, `Debug` | orquestación |
| `Evname`, `SeedRef`, `CmtFile`, `GfdirRel`, `FiltOrder`, `FiltPass`, `Idec2`, `Idec3`, `WpWin` | valores de `i_master`, con los defaults del generador del CSN |

En la GUI, el panel lee **el mismo `.d`** (`MwwConfigFile`, default `csnwphase.d`) y el
directorio de las corridas sale de `MwwWorkDir` (en `csnhypodbp.d`: `../csnhypodbp/mww`, es
decir `run_working_v8/csnhypodbp/mww`, relativo a `$EW_PARAMS`; el default del código es
`/tmp/csnhypodbp_mww`).

## 3. CLI

```bash
source ./ew8_unix.sh
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --print-config
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --prep  <rundir> <ISO> <lat> <lon> <prof> <mag> <nombre>
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --fetch <rundir>
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --run   <rundir> [--dry-run]
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --parse <rundir>
make -C ew_gui_tools/csnwphase check          # unitarios + tests contra los binarios reales del motor
make -C ew_gui_tools/csnwphase check-engine   # e2e del grid search contra el motor real (lento)
make -C ew_gui_tools/csnwphase check-corpus   # 3 eventos historicos M>=6.5 de punta a punta (lento)

# Corpus de validación (eventos históricos del catálogo, ondas del SDS)
ew_gui_tools/csnwphase/tools/gen_corpus.sh   /tmp/corpus.tsv   # M>=6.5 de los últimos 10 años
ew_gui_tools/csnwphase/tools/run_corpus.sh   /tmp/corpus.tsv   # corre y deja corpus.csv
ew_gui_tools/csnwphase/tools/corpus_report.sh corpus.csv       # Mww vs magnitud del catálogo
```

## 4. Qué deja en el run dir

```
<rundir>/i_master  CMTSOLUTION  run.sh        # entrada y la secuencia exacta (auditable)
<rundir>/DATA_org/*.SAC + SAC_PZs_*           # trazas y respuestas
<rundir>/run.log                              # salida del motor
<rundir>/WCMTSOLUTION                        # solución tras el screening
<rundir>/xyz_WCMTSOLUTION                    # solución 3D (grid search, GridSearch 1)
<rundir>/xy_WCMTSOLUTION                     # centroide refinado (la que prefiere el parser)
<rundir>/LOG/wpinversion*.log  LOG/gs_o_wpinversion.log
<rundir>/gs_SYNTH/  grid_search_{ts,xyz,xy}_out  # sintéticos y grillas del grid search
<rundir>/full_output.txt                      # salida COMPLETA (lo que muestra la GUI)
<rundir>/wphase.json                          # contrato estable para GUI y BD
```

La secuencia de `run.sh` sigue la de `RUNA3r.csh` (`decim_one_sac_file_to_1sps` →
`trim_sac_files` → `make_resp_lookup_table` → `rec_dec_filt` → `prep_kernels` →
`wpinversion -med` ⇒ **una sola** corrida de screening con `-th` y `-nr` juntos) con **dos
desviaciones deliberadas**:

1. **screening en una sola ejecución** (antes: tres `-th 5 3 1` encadenadas más una `-nr`).
   El motor aplica ambos flags en la misma corrida, en secuencia
   (`wpinversion.c:100-103`: `screen_rms` y luego `screen_ratio` sobre la solución de `-med`),
   y su log final es `LOG/wpinversion.log` — el que lee el parser cuando no hay grid search.
2. **grid search 3D**, que replica el `--xyz` de `wp_grid_search.py` (python2, no se usa) con
   **dos pasadas** de `wpinversion_gs`:
   - pasada 3D: `-oxygsf grid_search_xyz_out -dz <GsXyzDz> -minz <GsXyzMinz>` (la profundidad
     no tiene flag propio en el binario: entra por `-dz` en la misma pasada xy) ⇒ su solución
     se copia a `xyz_WCMTSOLUTION`;
   - pasada xy fina sobre esa solución (más las líneas ≥7 del CMT de referencia, si trae la
     solución completa: el `addrefsol` del wrapper) con `-nots -oxygsf grid_search_xy_out` ⇒
     `xy_WCMTSOLUTION`, el centroide que prefiere el parser.

   Así `grid_search_xyz_out` deja de ser un nombre huérfano en la captura de `full_output.txt`.

   Las dos invocaciones de `wpinversion_gs` llevan además `-hdsafe` y `-old`, como la
   producción del CSN (`GsHdsafe`/`GsOld`), y el tope del barrido ts sale de la magnitud
   (`GsTsAuto`); el screening (`-th` + `-nr`) sigue siendo una sola corrida.

## 5. Trampas (todas medidas en este repo)

- **El cmtfile de la pasada xy fina lleva EXACTAMENTE 13 campos**: el lector del motor
  (`read_i_files.c:238-247`, `get_cmtf` con `flag==2`) cuenta 1 (`PDE`) + 6 (`event name`,
  `time shift`, `half duration`, `latitude`, `longitude`, `depth`) + 6 (`Mrr`..`Mtp`) y
  **aborta con `ERROR: reading cmtfile … (field(s) redundancy)`** si alguno se repite. El
  `addrefsol` del CSN apendea `L[7:]` (solo los 6 momentos, `wp_grid_search.py:127`) sobre una
  cabecera de **7 líneas sin momentos** (`EQ.py:46-59`); copiar la salida completa del motor
  (13 líneas, ya con momentos) y apendar desde la línea 7 daba **20 campos** y el paso 10
  fallaba en **todas** las corridas (medido 2026-10-16 en dos run dirs: `field(s) redundancy`
  + `fin run.sh (rc=256)`, con `grid_search_xy_out` ausente). Hoy el `run.sh` hace
  `head -n 7 xyz_WCMTSOLUTION` + `tail -n +8 CMTSOLUTION`; si la referencia trae <13 líneas
  quedan 7 campos y el motor degrada solo a `flag==1` (`NO REF. SOL. USED`, `:99`).
- **`-old` exige la lista de 13 columnas**: reusa `az/gcarc/p2p/avg/wgt` de `o_wpinversion`
  (`wpinversion_sub.c:2814-2904`); con una lista de 1 columna el fallo es opaco, así que el
  `run.sh` lo comprueba antes y aborta con su propio mensaje.
- **Offsets del bloque de texto del SAC**: son **índices de palabra** y hay que
  contarlos sobre el `SACHEAD` real (`sac.h`: 158 palabras). `kstnm`=110, `kevnm`=112
  (4 palabras), `khole`=116, `ko`=118, `kcmpnm`=150, `knetwk`=152, `kdatrd`=154,
  `kinst`=156. Un desalineamiento aquí **no rompe nada visible**: el motor escribe
  `scr_dat_fil_list` con menos campos (`file sta net cmp locid` + 5 números) y
  `rec_dec_filt` muere con `ERROR: reading scr_dat_fil_list` porque
  `check_scan(10, n)` hace `exit(1)`. El test `tests/test_sac_engine.sh` compara los
  campos **uno a uno** contra el `saclst` del motor: cuidado con comparar la salida
  cruda, porque `saclst` imprime el **nombre del fichero** y `C1`/`AF02`/`BHZ` están
  en el nombre ⇒ pasarían por vacuidad.
- **`kevnm` va en blanco** (como lo deja rdseed): el motor arma el id de estación
  como `kstnm`+`kevnm`; poner ahí la estación produce `AP01AP01` y confunde el árbol.
- **`trim_sac_files` exige `delta == 1 s`** ⇒ el orden `decim → trim` es obligatorio.
- **`prep_kernels` hace `crea_dir(GFDIR)`** (borra y recrea) ⇒ `GFDIR` (`GfdirRel`) es
  siempre un subdirectorio del run dir (`./GF`), **nunca** `GfDir`. Hay guardia
  (`csnwphase_gfdir_guard`) por nombre, resolución y ruta absoluta.
- **`pzfile2id` exige 7 guiones bajos** en el nombre del `.pz` y arma el id
  `NET_STA_LOC_CHAN` (el mismo que consulta `makeid`). Locations vacías se escriben en
  blanco (no `-12345`) para que `nbchar()` dé 0 y el id use `--`.
- **`-pdata` es un fichero de SALIDA** (datos predichos), no una plantilla de entrada.
- **`make_resp_lookup_table` devuelve `rc=1` SIEMPRE**, aunque escriba la LUT completa (medido
  2026-10-13: con la lista completa, con una sola estación y con lista vacía). El `run.sh` lo tolera
  **sólo** si `coeffs_rec_lut` quedó con contenido (envoltorio `lut` de `csnwphase_run.c`): con
  `set -e` a secas el pipeline **abortaba ahí** y la inversión no arrancaba nunca, sin un solo
  `ERROR` en el log (síntoma: números por todas partes y `fin run.sh (rc=256)`).
- **El location de la traza debe ser `--`, no vacío**: `wpinversion` busca cada GF como
  `<STA>.<NET>.<CHAN>.<LOC>` usando el location **de la traza**, y el motor nombra los GF con `--`
  para el location nulo. La lista de estaciones (`StaFile`) **no** debe normalizar `--`→`""` —
  `ewgui_sds_path` ya traduce `--` a vacío para el SDS y el store lo quiere tal cual. Con vacío:
  `ERROR opening ./GF/gf_rr/STA.NET.CHAN..SAC.sac.bp` y `rc=1` (visto con `CX.PB16`, la estación que
  sólo entra con el hipocentro de la BD; con el PDE no está en la lista).
- **Cada paso del `run.sh` deja su marca** (`##### paso N: <orden> #####` y `##### paso N: rc=X #####`)
  y el panel `Mww` muestra la orden que falló: si el motor muere en silencio, el paso se identifica
  en el log sin instrumentar nada más.
- `timeout` del PATH puede ser el de SeisComP ⇒ el repo lo blinda: `ew8_unix.sh` antepone
  `ew_gui_tools/common/tools/gnubin` (shim a `/usr/bin/timeout`) y los scripts usan la ruta absoluta
  `/usr/bin/timeout` (`csnwphase_run.c` lanza el motor con ella).

## 6. GUI: panel `Mww`

*Procesar → magnitudes*: el área central es una pila con dos páginas
(`Estaciones` | `Mww`). El panel `Mww` trae:

- la **tabla de parámetros** de la corrida (misma fuente que `i_master`: config, evento,
  derivados y grid search) **editable**: cada fila de un `CSNWPHASE_PF_*` lleva su widget, una
  **caja de texto** (ya no el `GtkSpinButton` con `+`/`-`; las cajas se dejan anchas, sin
  `max-width-chars`), y lo que se edita se vuelca sobre la corrida
  (`csnhypodbp_mww_over.c`), así que la tabla, el `i_master` y el `run.sh` siguen teniendo una
  sola fuente. Son editables el **t0 (UTC)**, **lat**, **lon**, **profundidad**, **magnitud
  semilla**, **ventana de ondas** (pre/post), **screening RMS**, **screening ratio** y el
  **cribado de estaciones `DMIN`/`DMAX`** (dos filas, cada una con su caja, pegadas al final de
  los editables); la **banda de inversión** es derivada de la magnitud (se recalcula al editarla)
  y el resto es informativo. El rango y el parseo de cada caja viven en el núcleo puro
  (`mww_field_limits`/`mww_field_parse`, `csnhypodbp_mww_over.{h,c}`): un texto inválido o fuera
  de rango **no** se aplica, la celda se marca en rojo y ese campo vuelve al valor de la
  solución/`.d` (`mww_over_unset`), para que un texto a medias no deje aplicado un prefijo.
  Al cambiar de solución elegida los ajustes se descartan. El t0 se aplica **solo**
  si el texto es una fecha válida (la celda se marca en rojo si no lo es);
- **`Calculate`** (corre `input_prep` + el motor en un hilo, con indicador de progreso) y
  **`Cancel`** (descarta el resultado en curso: el `TimeoutSec` acota la corrida);
- el **resumen** (Mw, M0, centroide, planos nodales, estaciones usadas) con la
  **playera** dibujada en cairo desde el tensor de la solución, **al lado** del visor de la
  salida completa del motor (`full_output.txt`) y ambos con la misma altura (`MWW_ROW_H`, 248 px:
  el bloque de resultados +50 %; los dos marcos llevan `vexpand` y el bloque `row-homogeneous`,
  porque un `GtkGrid` no reparte el alto que sus hijos no piden: sin eso el bloque solo subía, sin
  crecer). El
  alto sobrante es para la tabla de parámetros, que es lo que se ajusta antes de calcular. Solo el
  cuadro **izquierdo** lleva la fuente grande, al **134 %** (un 70 % más que el 79 % anterior); la
  salida del motor se queda al **79 %**. El reparto es **1/3 – 2/3**: los dos
  cuadros viven en un `GtkGrid` de columnas **homogéneas** (reparte el ancho asignado por igual
  entre sus tres columnas no vacías, así que el de resultados, 1 columna, se queda con un tercio,
  y la salida, 2 columnas, con dos tercios; el reparto aguanta el redimensionado). El cuadro de
  resultados se queda con su tercio y el resumen se ajusta por líneas dentro de él —su
  `GtkScrolledWindow` usa `GTK_POLICY_EXTERNAL` y `min_content_width` 90 px, así no saca barra
  horizontal y no multiplica por tres el mínimo de la ventana—, con el texto y la playera
  separados por 4 px. Cualquier fallo (preparación,
  ondas o motor) se muestra con su texto real en la línea de estado.

**El mecanismo focal se guarda al confirmar la solución, no con un botón aparte** (el
`Save to DB` ya no existe). El preview del tope de la pestaña *Ondas* tiene **cinco** radios de
magnitud preferida — `ML`, `Mwp`, `Mb`, `Ms` y **`Mww`** — más una columna `Mww` que muestra el
valor de la inversión. El radio `Mww` solo se activa si hay **una corrida de fase W de la
solución que se está confirmando** (mismo `qid` y `mod`; el panel no borra el resultado viejo al
cambiar de selección, así que sin esa comprobación se le atribuiría a una solución el Mww de
otra) y, al pulsar `Confirm` con él marcado, se guarda el mecanismo focal y la magnitud:

- el **origen** es el de la **solución confirmada**, no el centroide de la inversión. El ARC
  confirmado no pasa por ningún anillo, así que `hypodb_ingest` no escribe ese origen: si no
  está en la base **se crea** (`event` + `origin`) con los ids de `ewgui/db_ids.h` — los mismos
  del ingester — y si ya está **no se pisa** (`ewguidb_upsert_origin_if_absent`);
- `Mww` **no** es un 5.º slot de magnitud de la GUI: no es una magnitud por estación con ventana
  y puerta, así que no toca `nmag0..3` ni la tabla *Origins*;
- `Mww` **tampoco viaja en el ARC**: `BMTYPI` es un solo carácter con `L`(ML)/`W`(Mwp)/`B`(Mb)/
  `S`(Ms), así que la picada sale sin tipo preferido y la preferencia se marca en la base
  (`ewguidb_set_preferred`). Ver `docs/pendienteMww.md`.

En la BD quedan dos filas con la convención de ids de `ewgui/db_ids.h`:

```
magnitude: <origin_id>/mag/Mww       type Mww, calculation_method WPHASE, station_count = n_used
tensor:    <origin_id>/tensor/WPHASE method WPHASE, mechanism WCMT,
           strike/dip/rake 1-2, m_rr..m_tp, scalar_moment
```

El resultado de la persistencia se muestra en la línea de estado del panel `Mww` (y un fallo
queda además en el log del módulo con su texto real), porque el `Confirm` vive en otra pestaña.

## 7. Verificación

- `make -C ew_gui_tools/csnwphase check`: config, guardia de GF, `i_master`/`CMTSOLUTION`
  byte a byte contra el generador del CSN, parser + `wphase.json`, captura de la salida
  completa, `.pz`→`SAC_PZs_*` y SAC validados con los binarios **reales** del motor
  (`make_resp_lookup_table`, `saclst`), ondas store/SDS y `run.sh` (dry-run).
- Corrida real del **M7.4 del 2024-07-19 01:50:47** (`-23.05, -67.98, 117 km`): 93
  estaciones en [5,50]°, 159 canales tras deconvolución y filtrado, **Wmag 7.35**
  (RMS 0.089 mm) y **7.48** tras el screening `-nr 2` (RMS 0.045 mm) ⇒ coherente con la
  magnitud publicada (7.4).
- **Grid search del centroide** sobre esa misma corrida (`wpinversion_gs`, `-ts 1 4 30`,
  `-dx 0.4 -Nx 3 -Nopt 5`): **Mw 7.47**, RMS 0.043 mm (0.365 %), 13 canales, y el
  centroide se desplaza a **-23.15, -67.76, 120.5 km** (shift 14 s) con los planos
  nodales `180.7/20.9/-78.7` y `348.7/69.5/-94.3`. El parser lo prefiere sobre
  `WCMTSOLUTION`.
- **La pasada 3D y la pasada xy fina están cubiertas por dos vías complementarias**:
  - `tests/test_run_dry.sh` (orden, `.xyz` intermedio, apéndice de la referencia,
    `xyz_WCMTSOLUTION`) **más el contrato de 13 campos** del cmtfile temporal, simulado con el
    fixture: cuenta cada campo reconocido y exige que aparezca **una sola vez**;
  - `make check-engine` (`tests/test_engine_gs.sh`, lento): ejecuta el bloque del grid search
    del `run.sh` **generado ahora** contra el **motor real**, sobre un run dir montado con
    enlaces a una corrida anterior, y exige `grid_search_xyz_out` + `grid_search_xy_out` +
    `xy_WCMTSOLUTION` + `Wmag:` y ningún `ERROR: reading cmtfile`. Medido 2026-10-16 sobre
    `run_working_v8/csnhypodbp/mww/0801000068-20261016T131725`: la grilla 3D reproduce la del
    run original (misfit `0.00009391`, igual con y sin `-hdsafe`/`-old`) y la xy fina **sí**
    se escribe (antes no existía: el paso 10 abortaba).
- **Corpus histórico** (`tools/gen_corpus.sh` + `run_corpus.sh` + `corpus_report.sh`,
  `make check-corpus`): eventos M≥6.5 de los últimos 10 años del catálogo histórico
  (`EW8_GUItools`, 36 eventos), ondas del SDS y el pipeline completo por evento. Primera
  corrida medida (2026-10-16; M6.5 del 2023-03-22, 240 km): 102 estaciones en [5,50°],
  184 trazas, **`n_used` 28, Mww 6.32** frente a 6.5 del catálogo (|ΔM| 0.18) en 85 s.
- `gui_check_mww.sh` valida el panel con AT-SPI contra el EarthWorm en marcha
  (`--dump` y `--shot` para calibrar/capturar). **En este entorno el puente AT-SPI no
  expone los módulos GTK4 y los clics sintéticos no llegan**, así que la revisión visual
  del panel se hace en la sesión real.

## 8. Estado

Sin pendientes funcionales de fase W: el screening es una sola corrida (`-th` + `-nr`), el
grid search del centroide está integrado **en 3D** (pasada `-dz`/`-minz` → `grid_search_xyz_out`
más pasada xy fina; el wrapper del CSN `wp_grid_search.py` no se usa porque es python2), el
parser prefiere `xy_WCMTSOLUTION`, la invocación del motor va como la de producción del CSN
(`-hdsafe` + `-old`, tope del barrido ts derivado de la magnitud) y hay validación contra el
motor real (`check-engine`) y contra eventos históricos (`check-corpus`). La única desviación
que queda es el screening en **una sola pasada** (los `-th 5 3 1` encadenados de `RUNA3r.csh`).

Lo que **no** está cerrado es la integración del **`Mww` como magnitud de primera clase**:
viaja en la BD (`magnitude.type = Mww`) pero no en el ARC, así que la única marca de
preferencia es `ewguidb_set_preferred` y ningún otro módulo lo lee. Ver
`docs/pendienteMww.md`.

El único límite es de entorno: el motor y su base de funciones de Green (`GfDir`, ~21 GB)
viven **fuera del repo**, así que en una máquina que no los tenga el panel lo reporta en su
línea de estado y la corrida falla con ese motivo (no en silencio).
