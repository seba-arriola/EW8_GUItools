### Validación rápida con picks OFFLINE (mejorar `csnloc` en minutos, no horas)

Este es el **bucle de trabajo al modificar `csnloc`**. En vez de reproducir las formas de onda con
`tankplayer` (~10 h y dependiente de anillos), se capturan los picks **una sola vez** leyendo los
tanks directo con `pick_FP` y luego cada validación cuesta **segundos**:

```
master.tank ──pick_FP (offline)──► picks/<ts>/<slug>.picks       (una vez, ~7 s/tank)
                                        │
                                        ▼
      csnloc <csnloc.d> <slug>.picks    (reloj virtual; sin tankplayer, sin anillos)
                                        │
                                        ▼
   picks/<ts>/csnlocvalidate_<ts>/<slug>.jsonl ──► reporte / comparación
```

Capturas y validaciones viven en carpetas **inmutables con marca de tiempo**: nunca se pisan, así
que dos corridas de la misma captura se comparan entre sí.

```bash
source ./ew8_unix.sh

# 1) UNA VEZ: capturar los picks de todos los tanks (~5-10 min)
./tank_tools/capture_picks.sh tank_repo picks        # -> picks/20260924-120000/<slug>.picks
CAP=$(ls -1d picks/*/ | sort | tail -1)

# 2) validar con el csnloc ACTUAL (segundos)
make -C ew_gui_tools/csnloc
./tank_tools/validate_csnloc.sh "$CAP"               # -> $CAP/csnlocvalidate_<ts>/
V1=$(ls -1d "$CAP"/csnlocvalidate_*/ | sort | tail -1)

# 3) cambias csnloc, recompilas y validas OTRA VEZ sobre la MISMA captura
make -C ew_gui_tools/csnloc
./tank_tools/validate_csnloc.sh "$CAP"
V2=$(ls -1d "$CAP"/csnlocvalidate_*/ | sort | tail -1)

# 4) ver y comparar
python3 tank_tools/offline_report.py "$V1"                 # tabla de una corrida
python3 tank_tools/offline_report.py "$V1" "$V2"           # progreso V1 -> V2
python3 tank_tools/offline_report.py "$V1" "$V2" --format all --out /tmp/cmp
```

Atajos: `make -C tank_tools capture` · `make -C tank_tools validate CAPTURE=...` ·
`make -C tank_tools offline` (captura + valida) ·
`make -C tank_tools report-offline VALIDATION=... CONTRA=...`.

Reglas del flujo (clave para interpretar los resultados):

- **Nunca se cruzan tanks distintos**: la comparación es tank contra tank, mismo `<slug>`.
- Si no se toca `csnloc`, dos validaciones de la misma captura son **idénticas byte a byte** y
  `offline_report.py` lo dice (`*** CORRIDAS IDENTICAS (sin cambios) ***`). Cualquier diferencia es
  un cambio real de comportamiento.
- La ventana deslizante emite **varias soluciones del mismo sismo**; el reporte las agrupa por
  `Δt ≤ T` y `Δd ≤ D` (single-linkage) y el **representante es la última generada** (mayor `event`).
  `n_sol` = cuántas soluciones componen el evento.
- Clases A↔B: `MISMO` (Δt≤T y Δd≤D) · `MISMO_TIEMPO_AMPLIADO` (Δd≤D y T<Δt≤F·T) ·
  `TEMPORAL_SIN_ESPACIO` (Δt≤T y Δd>D) · `SOLO_A`/`SOLO_B`. Umbrales por CLI: `--time-window`
  (30 s), `--dist-deg` (1.0°), `--window-factor` (2.0).
- Cada validación guarda `manifest.json` (captura de origen, config usada y `sha256` del binario
  `csnloc`) más una copia de `csnloc.d`: queda registrado *qué* se comparó.
- No es regresión contra catálogo: es **observación de progreso**. El `.jsonl` trae `t0`, `lat`,
  `lon`, `depth_km`, `nphases`, `rms_sec`, `gap_deg`, `dmin_km`, `score` y las fases con su
  `residual`; **no** calcula magnitudes (eso necesita formas de onda: `run_events.sh` +
  `api_report_locs`).

#### Validación de `pickS` contra picadas manuales

`picks_por_tests410.dat` (raíz del repo) trae, por evento, la solución hipocentral
(`id  t0  lat  lon  prof  mag  tipo`) y picadas manuales `NET.STA.LOC.CHAN  ISO8601  P|S  manual`.
El ensayo completo:

```bash
source ./ew8_unix.sh
make -C tank_tools validate-manual                 # todo (lento: ~1-3 h)
make -C tank_tools validate-manual MANUAL_ONLY='test[1-30]'   # subconjunto
```

Encadena: `picks_s_manual.py events` → `ttp` (IASP91) → `gen_guide_p.py` (guía P) →
`capture_picks_s.sh --metrics-out` (independent + hybrid, con métricas intrínsecas) →
`picks_s_manual.py report` → `picks_s_manual.py calibrate`. Salidas en
`/tmp/opencode/picks_manual/report_{ind,hyb}/` (`report.csv`, `summary.json`, `good_s/`,
`confiable/`) y `calibrate_hyb.json`.

Reglas del reporte: el archivo es una **muestra gruesa** (apenas un mes), así que es un
**chequeo suelto** ("¿encontró una S que estaba en el archivo? ¿es parecida?"), no un
match estricto: el emparejamiento es **por nombre de estación** (una S manual en
`MT14.BHN` valida una S detectada en `MT14.HHN`); `resid_S = t_det − (t0 + ttS)`; no hay
categoría "FP" — lo no presente es `SIN_REF`. La **certeza es intrínseca** (parámetros):
`summary.json` trae `certeza` (`ALTA`/`BAJA`, `pureza_alta`), `metric_dist` y
`file_check`; `confiable/` guarda los picks `ALTA` y `good_s/` los de residual bajo.
`calibrate` barre umbrales (`Q_*`/`MinSnr`/`TriggerOn`) sobre keep+drop para ajustarlos
sin recompilar. `MetricsLog` expone las métricas por detección (solo log; no toca el pick
ni el anillo).

Herramientas: `tank_tools/picks_s_manual.py`, `tank_tools/gen_guide_p.py`,
`tank_tools/validate_manual.sh`, `ew_gui_tools/csnloc/ttpred`.

#### Picks de onda S (pickS)

`pickS` pica ondas S sobre los horizontales de los tanks y emite picks con el
11.º token de fase `S`, que `csnloc` ya interpreta. El flujo es análogo al de
`capture_picks.sh`, con dos añadidos:

```bash
source ./ew8_unix.sh
# barrido P+S combinado (P de pick_FP + S de pickS guiado por las P) -> picks_ps/<ts>/
./tank_tools/capture_picks_ps.sh tank_repo picks_ps
./tank_tools/validate_csnloc.sh picks_ps/<marca de tiempo>          # csnloc offline con P+S
make -C tank_tools offline-ps                                       # atajo (capture-ps + validate-ps)
# solo S o solo P (merge idempotente):
python3 tank_tools/merge_picks.py --out picks_ps/<ts> --phases S picks/<ts> picks_s/<ts>
```

Antes, hay que habilitar los horizontales en la configuración (una vez):
`./tank_tools/add_horizontal_config.sh --apply` (genera `pickS.sta`, añade
`Tank HHE/HHN` a `wave_serverV.d` —legado— y `HH?` a `slink2ew_HHZ.d`; reiniciar
EarthWorm después). Ver `ew_gui_tools/pickS/README.md`.

#### Tests de `csnloc`

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/csnloc check           # unitarios: parseo de picks, re-pick, DBSCAN,
                                            # formato HYP2000, tiempos de viaje, localizacion
                                            # sintetica y dos eventos
make -C ew_gui_tools/csnloc check-offline   # modo offline sintetico: determinista (2 corridas
                                            # identicas) y localiza dentro de la tolerancia
```

Ambos son **OFFLINE**: no arrancan servicios y se pueden correr en cualquier momento. Tras cambiar
`csnloc` el orden recomendado es `make check` → `make check-offline` → bucle offline con picks
reales (arriba). El modo offline es un camino de entrada aparte y **no altera el modo anillo**.

### Pruebas con eventos reales (repo de tanks)

Convierte una vez cada miniSEED de `mseed/` a un tank reutilizable y luego pasa los eventos en
modo rápido, dejando un reporte de localizaciones y magnitudes:

```bash
source ./ew8_unix.sh

# 1) UNA VEZ: mseed/ -> tank_repo/<slug>/{master.tank,manifest.json}
make -C tank_tools tank-repo                            # todos (~2 min/fichero, ~20-25 GB)
./tank_tools/build_tank_repo.sh --only 'simulacion3_*'  # uno concreto
./tank_tools/build_tank_repo.sh --dry-run --force       # ver que haria

# 2) planificar (no arranca nada)
./tank_tools/run_events.sh plan tank_repo
./tank_tools/run_events.sh plan tank_repo/test2

# 3) ejecutar  (admite un tank o un directorio)
./tank_tools/run_events.sh run tank_repo                  # interactivo: para tras cada evento
./tank_tools/run_events.sh run tank_repo --yes            # todos seguidos
./tank_tools/run_events.sh run tank_repo --mode realtime  # fiel (lento)
./tank_tools/run_events.sh run replay/tanks/master.tank   # un solo tank

# 4) reporte
python3 -m api_report_locs.report --runs runs --format all
```

#### Por que se trocea y por que se espera al final

Tres invariantes obligan al diseno (una sesion = un trozo, con stack nuevo):

| invariante | valor | fuente |
|---|---|---|
| convención del arnés: acotar el futuro que inyecta `fast` | `span + pared <= 900 + SendLate` | heredado de `wave_serverV.c:3024` (módulo **RETIRADO**) |
| `csnloc` asocia cada `AssocWindowSec/2 + 1` s de reloj de pared | >= 61 s de pared con 120 | `csnloc.c:340` |
| el ultimo pick necesita una pasada DESPUES | "asentamiento" = 61 + 4 s | idem |

- No se usa `--sequence`: con 0 el trozo re-anclado va hacia atras y con 1 el futuro se acumula
  (los topes los imponia `wave_serverV`, ya retirado; se conservan como convención del arnés).
- El troceo usa `-C HH` (span ~= duracion, sin paquetes de periodo largo que lo inflen) y se
  calcula sobre la **ventana del canal**, no sobre el span global: los canales LH/LN llegan mas
  lejos y el span global dejaria trozos vacios al final.
- `--mode fast` comprime el ruido: `delay_ms` se calcula para que la pared sea ~`--min-wall`.
  `--mode realtime` es fiel pero tarda el span completo (un fichero con 10 min de ruido = 20-30 min).
- Perfil de cribado rapido: `--assoc-window 40` baja la cadencia a 21 s y el asentamiento a 25 s
  (cada sesion ~40 s mas corta). **No cambia produccion**: solo la copia aislada de params.

#### Aislamiento

Cada tank usa una copia de `run_working_v8/params` en `runs/<slug>/params` (GUIs comentadas,
`Debug 1` y `DumpHypo 1` para tener la hora origen en el log), y cada sesion un `EW_LOG` propio
en `runs/<slug>/<NNN>/logs/`. **No se toca la configuracion ni los logs reales.** Los `.tnk` de
trabajo se borran al acabar cada tank (salvo `--keep-tanks`). El modo replay **detiene el
EarthWorm en marcha** y lo deja parado al terminar.

#### Reporte

`api_report_locs/` lee `runs/*/*/session.json` + los logs y produce tabla, CSV y JSON con
hipocentro, fases, RMS, gap, ML/Mwp, magnitud preferida y la **hora origen historica** (se resta
el `offsetTime` de `tankplayer`, porque el replay re-estampa el dato a "ahora"). El cruce
localizacion<->magnitud es por ID numerico **dentro de la sesion** (el contador de `csnloc` se
reinicia en cada arranque) y los duplicados por solape se marcan, no se descartan. Detalles en
`api_report_locs/README.md`.
