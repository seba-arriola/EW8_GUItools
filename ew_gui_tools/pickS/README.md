# pickS — picker automático de onda S

Módulo Earthworm nativo que detecta y pica **ondas S** con un criterio de
calidad por polarización 3C. Es el complemento del `pick_FP` (que pica P).

## Flujo

```
ondas SLINK_RING (TYPE_TRACEBUF2, HH?/BH?) ─┐
                                            ├─► pickS ─► PICK_RING (TYPE_PICK_SCNL, fase S)
picks P PICK_RING (TYPE_PICK_SCNL) ─────────┘
                                                 │
                                                 ▼
                                              csnloc
```

- **Entrada ondas**: `InRing` (default `SLINK_RING`). Se pican las componentes
  horizontales de las estaciones de `pickS.sta`.
- **Guía P** (`PickRing`): si hay algún pick P de la estación, el modo `hybrid`
  exige **siempre** una P previa de esa misma estación en la ventana post-P
  `[Tp+GuideMinDtSec, Tp+GuideMaxDtSec]`; sin P asociada el S se descarta (la S
  debe seguir a una P). Si no hay ninguna P en el anillo, la detección es
  independiente.
- **Salida** (`OutRing`): picks `S` al anillo de picks. El canal reportado es el
  horizontal donde se identificó el pick (`ReportChan detected`).

## Modos de ejecución

### Anillo (producción)

```
pickS <pickS.d>
```

Requiere `earthworm.d` cargado (`MOD_PICKS 165`) y los anillos enlazados.

### Offline sobre tanks (para calibrar y para csnloc)

```
pickS <pickS.d> <tankfile> [ppicksfile]
```

Lee el tank directo (`TRACE2_HEADER + int32`), sin anillos ni reloj de pared, y
escribe una línea `TYPE_PICK_SCNL` (fase `S`) por pick a **stdout**.
`ppicksfile` es opcional y aporta los picks P de guía (formato `.picks` de
`pick_FP`).

Cada corrida es **determinista**: misma config + mismo tank → salida byte a byte
idéntica.

## Algoritmo

1. **Buffer 3C sincronizado** por estación (`ring3c.c`): E/N/Z alineadas por
   tiempo de muestra, tolerando paquetes desordenados y gaps.
2. **Filtro** Butterworth pasa-banda en cascada (`FilterLowHz`/`FilterHighHz`/
   `FilterOrder`).
3. **Detección** STA/LTA sobre la envolvente horizontal `H = sqrt(E² + N²)`
   (`StaLenSec`/`LtaLenSec`/`TriggerOn`/`TriggerOff`).
4. **Refinamiento** del onset por **AIC** (`AicWinSec`).
5. **Polarización 3C** (Jurkevics) en `PolWinSec`: rectilinealidad, planaridad,
   incidencia, azimut y ratio H/V.
6. **Calidad** → weight 0-4 (0 = mejor): peor-caso de SNR, pico STA/LTA,
   rectilinealidad e incidencia, con umbrales `Q_*`. Rechazo si `SNR < MinSnr`
   o `weight > MaxWeight`. `planarity` y `hv_ratio` se calculan siempre y, si se
   habilitan (`Q_Plan_W0`/`Q_Hv_W0` > 0), entran también al weight.
7. **Anti-duplicados**: `DeadTimeSec` por estación.

## Configuración (`pickS.d`)

| Clave | Default | Descripción |
|---|---|---|
| `MyModuleId` | MOD_PICKS | Id de módulo (165) |
| `InRing` | SLINK_RING | Ondas de entrada |
| `PickRing` | PICK_RING | Picks P de guía |
| `OutRing` | PICK_RING | Salida de picks S |
| `HeartbeatInt` | 30 | Heartbeat (s) |
| `StaFile` | pickS.sta | Lista de estaciones 3C |
| `FilterLowHz` / `FilterHighHz` | 1.0 / 10.0 | Banda del filtro |
| `FilterOrder` | 4 | Orden (secciones = order/2) |
| `StaLenSec` / `LtaLenSec` | 0.5 / 20.0 | Ventanas STA/LTA |
| `TriggerOn` / `TriggerOff` | 3.5 / 1.5 | Umbrales de disparo |
| `DeadTimeSec` | 5.0 | Tiempo muerto por estación |
| `AicWinSec` | 3.0 | Ventana AIC |
| `BufferSec` | 60.0 | Buffer 3C por estación |
| `GuideMode` | hybrid | `independent` / `hybrid` |
| `GuideMinDtSec` / `GuideMaxDtSec` | 0.5 / 60.0 | Ventana post-P |
| `PolWinSec` | 1.5 | Ventana de polarización |
| `MinSnr` | 3.0 | SNR mínimo |
| `MaxWeight` | 4 | Weight máximo aceptado |
| `ReportChan` | detected | `e` / `n` / `detected` |
| `Q_Snr_W0/W4` | 10 / 2 | Umbrales SNR para w0/w4 |
| `Q_StaLta_W0/W4` | 8 / 2 | Umbrales STA/LTA |
| `Q_Rect_W0/W4` | 0.85 / 0.30 | Umbrales rectilinealidad |
| `Q_IncidMinDeg/MaxDeg` | 45 / 135 | Rango de incidencia (grados) |
| `GateIncidMinDeg/MaxDeg` | 0 / 0 | Compuerta dura de incidencia (Max≤Min = off) |
| `Q_Plan_W0/W4` | 0 / 0 | Planaridad (0 = deshabilitada) |
| `Q_Hv_W0/W4` | 0 / 0 | Ratio H/V (0 = deshabilitado) |
| `MetricsLog` | 0 | Línea `pickS: METRICS …` por detección (calibración) |

> `MetricsLog 1` escribe una línea por detección (mantenida y descartada) con
> `snr`, `stalta`, `rect`, `plan`, `inc`, `azi`, `hv`, `dtsp`, `w` y
> `verdict=keep|drop`. Es **solo observabilidad**: no altera el pick, el mensaje
> `TYPE_PICK_SCNL` ni el anillo. En modo offline sale a stdout; en anillo, al log.

`pickS.sta` (una línea por estación 3C):

```
PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
1 1 FAR1 C -- HHE HHN HHZ
```

`PickFlag 0` = ignorar (mismo criterio que `pick_FP.sta`).

## Formato de salida

Idéntico a `pick_FP` más el 11.º token de fase:

```
8 <modid> <instid> <seq> STA.CHAN.NET.LOC <fm><wt> YYYYMMDDhhmmss.mmm <amp> 0 0 S
```

Compatible con `capture_picks.sh`, con el parser de `csnloc` (`pick_scln.c`,
token de fase) y con el regex `PICK_RE`.

## Herramientas offline

```bash
source ./ew8_unix.sh

# capturar picks S de un repo de tanks (y guiar con los P de picks/)
./tank_tools/capture_picks_s.sh --ppicks picks tank_repo picks_s

# validar con csnloc OFFLINE (misma captura)
./tank_tools/validate_csnloc.sh picks_s/<marca de tiempo>

# atajos
make -C tank_tools capture-s
make -C tank_tools validate-s CAPTURE_S=picks_s/<marca de tiempo>
make -C tank_tools offline-s
```

## Configuración de horizontales en el pipeline

Las horizontales existen en los tanks, pero hay que habilitarlas en adquisición
y almacenaje. El integrador lo hace de forma idempotente:

```bash
./tank_tools/add_horizontal_config.sh --dry-run   # solo informa
./tank_tools/add_horizontal_config.sh --apply     # con respaldos .bak.<fecha>
```

Aplica:
1. Genera `pickS.sta` desde `estaciones_107.txt`.
2. Añade líneas `Tank ... HHE/HHN` a `wave_serverV.d`.
3. Amplía los selectores de `slink2ew_HHZ.d`: `HHZ.D` → `HH?.D`.

Tras `--apply` hay que **reiniciar EarthWorm** para que `slink2ew` pida los
horizontales y `wave_serverV` cree los `.tnk` nuevos.

## Validación contra picadas manuales

`picks_por_tests410.dat` (raíz del repo) permite medir qué tan bien pica `pickS`:
por cada evento trae la solución hipocentral y las picadas manuales P/S.

```bash
source ./ew8_unix.sh
make -C tank_tools validate-manual                       # todo (lento)
make -C tank_tools validate-manual MANUAL_ONLY='test[1-30]'
```

- `ew_gui_tools/csnloc/ttpred` predice los tiempos IASP91 P/S desde la solución
  conocida (`resid_S = t_det − (t0 + ttS)`).
- El emparejamiento con las S manuales es **por nombre de estación** (ignora
  canal), para relacionar p. ej. `MT14.BHN` con `MT14.HHN`.
- El archivo es una **muestra gruesa** (apenas un mes): se usa como **chequeo
  suelto** ("¿encontró una S que estaba en el archivo? ¿es parecida?", vía
  `dt_manual`/`resid_S`). No hay categoría "FP": lo no presente es `SIN_REF`.
- La **certeza es intrínseca** (parámetros de la detección): `summary.json` trae
  `certeza` (`ALTA`/`BAJA`, `pureza_alta`) y `metric_dist`. `confiable/` guarda
  los picks `ALTA` y `good_s/` los de residual bajo.
- `calibrate` barre umbrales intrínsecos sobre **todas** las detecciones
  (`keep`+`drop`) y muestra, por umbral, cuántas pasan y su tasa `en_ref`, para
  ajustar `Q_*`/`MinSnr`/`TriggerOn` sin recompilar.
- La captura con métricas: `capture_picks_s.sh --metrics-out DIR` (fuerza
  `MetricsLog 1` en una copia temporal de la config; producción queda intacta).

## Pasar las S a los localizadores (offline)

En vivo, `pickS` publica en `PICK_RING` y `csnloc` las consume directamente (11.º token
`S`). En el **modo offline**, `csnloc` lee **un `.picks` por slug**, así que P y S deben
combinarse:

```bash
# barrido P+S sobre el repo de tanks (P de pick_FP + S de pickS, guiado por las P)
make -C tank_tools capture-ps                  # -> picks_ps/<ts>/<slug>.picks
# o reusando una captura P ya existente:
./tank_tools/capture_picks_ps.sh --pdir picks/<captura_P> \
    --sdir picks_s/<ts> --out picks_ps/<ts> tank_repo

# combinar con control de fase (idempotente: omite lo ya presente)
python3 tank_tools/merge_picks.py --out picks_ps/<ts> --phases PS <dirP> <dirS>

# localizar con P+S
make -C tank_tools offline-ps
```

`--phases PS|P|S` permite pasar solo P o solo S. Ver `tank_tools/RUNBOOK.md` §9.

## Tests

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/pickS check            # unitarios (filtro, STA/LTA, AIC,
                                            # polarización, calidad, buffer,
                                            # formato, lista de estaciones)
make -C ew_gui_tools/pickS check-offline    # integrado offline determinista
```
