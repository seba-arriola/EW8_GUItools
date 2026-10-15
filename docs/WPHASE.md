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
| `Dmin`, `Dmax`, `MinMagnitude` | cribado de estaciones y umbral de disparo |
| `WinPre`, `WinPost` | ventana de ondas alrededor del origen |
| `ScreenTh`, `ScreenNr` | screenings del motor (`RUNA3r.csh`: `5 3 1` y `2`) |
| `GsTsMin`, `GsTsDt`, `GsTsMax`, `GsTsNit`, `GsXyDx`, `GsXyNx`, `GsXyNopt`, `GsXyNit` | **grid search del centroide** (`wpinversion_gs`); los `-ts` los derivaba el wrapper del CSN de la duración, aquí son visibles y editables |
| `GridSearch`, `TimeoutSec`, `KeepTmp`, `MaxConcurrent`, `Debug` | orquestación |
| `Evname`, `SeedRef`, `CmtFile`, `GfdirRel`, `FiltOrder`, `FiltPass`, `Idec2`, `Idec3`, `WpWin` | valores de `i_master`, con los defaults del generador del CSN |

En la GUI, el panel lee **el mismo `.d`** (`MwwConfigFile`, default `csnwphase.d`) y el
directorio de las corridas sale de `MwwWorkDir` (default `/tmp/csnhypodbp_mww`).

## 3. CLI

```bash
source ./ew8_unix.sh
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --print-config
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --prep  <rundir> <ISO> <lat> <lon> <prof> <mag> <nombre>
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --fetch <rundir>
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --run   <rundir> [--dry-run]
ew_gui_tools/csnwphase/csnwphase run_working_v8/params/csnwphase.d --parse <rundir>
make -C ew_gui_tools/csnwphase check          # unitarios + tests contra los binarios reales del motor
```

## 4. Qué deja en el run dir

```
<rundir>/i_master  CMTSOLUTION  run.sh        # entrada y la secuencia exacta (auditable)
<rundir>/DATA_org/*.SAC + SAC_PZs_*           # trazas y respuestas
<rundir>/run.log                              # salida del motor
<rundir>/WCMTSOLUTION                        # solución tras el screening
<rundir>/xy_WCMTSOLUTION                      # centroide (grid search, GridSearch 1)
<rundir>/LOG/wpinversion*.log  LOG/gs_o_wpinversion.log
<rundir>/gs_SYNTH/  grid_search_{ts,xy}_out  # sintéticos y grillas del grid search
<rundir>/full_output.txt                      # salida COMPLETA (lo que muestra la GUI)
<rundir>/wphase.json                          # contrato estable para GUI y BD
```

La secuencia de `run.sh` es la de `RUNA3r.csh`: `decim_one_sac_file_to_1sps` →
`trim_sac_files` → `make_resp_lookup_table` → `rec_dec_filt` → `prep_kernels` →
`wpinversion -med` → screenings `-th 5 3 1` → screening `-nr 2` → `wpinversion_gs`
(grid search del centroide, si `GridSearch 1`) ⇒ `xy_WCMTSOLUTION`.

## 5. Trampas (todas medidas en este repo)

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
  derivados y grid search) y la **magnitud semilla editable**: decide la banda del filtro y
  la half-duration, se precarga con la de la solución elegida (o `MinMagnitude`) y **no**
  bloquea `Calculate`;
- **`Calculate`** (corre `input_prep` + el motor en un hilo, con indicador de progreso) y
  **`Cancel`** (descarta el resultado en curso: el `TimeoutSec` acota la corrida);
- el **resumen** (Mw, M0, centroide, planos nodales, estaciones usadas) con la
  **playera** dibujada en cairo desde el tensor de la solución;
- el **visor de la salida completa** del motor (`full_output.txt`); cualquier fallo
  (preparación, ondas o motor) se muestra con su texto real en la línea de estado;
- **`Save to DB`**: escribe en la BD **la solución que se está analizando** (sus
  parámetros hipocentrales ya viven en `origin`: sólo se enlaza por su `origin_id`) más
  lo que aporta la fase W, es decir la **magnitud** y el **mecanismo focal**. El
  **centroide no se escribe** en `origin`.

En la BD quedan dos filas con la convención de ids de `hypodb_ids.c`:

```
magnitude: <origin_id>/mag/Mww       type Mww, calculation_method WPHASE, station_count = n_used
tensor:    <origin_id>/tensor/WPHASE method WPHASE, mechanism WCMT,
           strike/dip/rake 1-2, m_rr..m_tp, scalar_moment
```

`Mww` **no** es un 5.º slot de magnitud de la GUI: no es una magnitud por estación con
ventana y puerta, así que no toca `nmag0..3` ni la tabla *Origins*.

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
- `gui_check_mww.sh` valida el panel con AT-SPI contra el EarthWorm en marcha
  (`--dump` y `--shot` para calibrar/capturar). **En este entorno el puente AT-SPI no
  expone los módulos GTK4 y los clics sintéticos no llegan**, así que la revisión visual
  del panel se hace en la sesión real.

## 8. Estado

Sin pendientes funcionales de fase W: el grid search del centroide está integrado
(`wpinversion_gs`; el wrapper del CSN `wp_grid_search.py` no se usa porque es python2) y
el parser prefiere `xy_WCMTSOLUTION`.

El único límite es de entorno: el motor y su base de funciones de Green (`GfDir`, ~21 GB)
viven **fuera del repo**, así que en una máquina que no los tenga el panel lo reporta en su
línea de estado y la corrida falla con ese motivo (no en silencio).
