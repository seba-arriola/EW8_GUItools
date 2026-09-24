# Runbook — Pruebas de `csnloc` con tanks reales

Herramientas: `tank_tools/` (replay + runner) y `api_report_locs/` (reporte).
Objetivo: inyectar sismos reales (miniSEED) en la cadena `pick_FP → csnloc → HYPO_RING`
sin depender de SeedLink, y leer las localizaciones y magnitudes que produce.

---

## 0. Primero: las tres dudas de fondo

### 0.1 ¿Estos tests tienen que ver con mis módulos? **No. Son dos mundos distintos.**

| Mundo | Qué es | Su test |
|---|---|---|
| **Tu localizador** | `ew_gui_tools/csnloc/` (el código que tú modificas) + `csnmags_toy` | `make -C ew_gui_tools/csnloc check` → 7 tests en C (`test_pick_parse`, `test_repick`, `test_dbscan`, `test_hypo_format`, `test_tt`, `test_synth_locate`, `test_two_events`) |
| **El banco de pruebas** | `tank_tools/` (replay/runner) + `api_report_locs/` (reporte) | `make -C tank_tools test` → 3 familias de tests en bash + 1 suite Python |

`tank_tools` **no es tu localizador**. Es la infraestructura que *alimenta* a tu localizador
con sismos reales y luego *lee* lo que produjo. Sus tests solo comprueban que ese banco de
pruebas no está roto.

### 0.2 ¿Por qué hay tantos tests diferentes?

Porque cada familia protege **una trampa concreta** que ya nos mordió:

| Test | Protege contra… | ¿Arranca EarthWorm? |
|---|---|---|
| `test_remux.sh` | Que el tank quede **desordenado** y `tankplayer` lo reproduzca mal | No (offline) |
| `test_fastmode.sh` | Que en modo `fast` los paquetes **futuros** sean rechazados por `wave_serverV` (>900 s) | Parte sí (live) |
| `test_chan_align.sh` | Que el canal **HHZ/BHZ** esté incoherente y no se calculen magnitudes | Parte sí (live) |
| `py-test` (Python) | Que el **reporte** cruce mal localizaciones con magnitudes | No (offline) |

No tienes que correrlos todos siempre. Ver §2.

### 0.3 ¿Por qué tantas opciones? **Porque casi nunca las usarás.**

Cada comando trae valores por defecto pensados para este repo. En el día a día pasarás
**0 o 1 opción**. En este runbook marco con ⭐ las pocas que sí importan y con ⚙️ las que
puedes ignorar.

---

## 1. El flujo completo (mapa mental, 5 cajas)

```
   miniSEED            tank            tankplayer          csnloc           reporte
  (mseed/)  ──①──►  (tank_repo/)  ──②──►  reproduce  ──►  localiza  ──►  (runs/)  ──③──►  tabla/CSV
```

- **① Construir** los tanks (se hace **una vez**, es lo caro). → §3
- **② Reproducir** un evento o todos, rápido o fiel. → §4
- **③ Reportar** resultados. → §5

Y aparte, dos secciones de apoyo: **§2 verificar que el banco está sano** y **§6 qué hacer
cuando algo falla**.

---

## 2. Sección de verificación (¿está sano el banco de pruebas?)

Esto **no** prueba tu localizador; prueba las herramientas de replay. Se corre tras un cambio
en `tank_tools/` o tras un susto. **Comando único:**

```bash
source ./ew8_unix.sh
make -C tank_tools test
```

**Qué hace:** lanza `tests/run_all.sh` en modo **OFFLINE** (no arranca ningún servicio, no toca
tu configuración ni tus logs reales). Dura ~1 min.

**Por qué existe:** confirma que la conversión, el modo rápido y la coherencia de canal siguen bien.

**Opciones** (son *targets* del Makefile, no flags):

| Target | Qué añade | Cuándo usarlo |
|---|---|---|
| `make -C tank_tools test` | Solo lo **offline** (~1 min) | ⭐ Siempre, es tu verificación base |
| `make -C tank_tools test-slow` | Añade las offline **lentas** (mseed real, ~45 s, ~1,3 GB) | Antes de una campaña de pruebas larga |
| `make -C tank_tools test-live` | Añade las **live**: **arranca y DETIENE EarthWorm** (~15 min) | Solo si quieres probar de punta a punta; **no en operación** |
| `make -C tank_tools py-test` | Los 20 tests del **reporte** Python | Tras tocar `api_report_locs/` |

**Criterio de éxito:** termina con `exit 0` y **0 FAIL** (algunos se marcan `SKIP`, es normal:
son los live que no pediste).

**Y el test de tu localizador** (el que sí te importa):

```bash
make -C ew_gui_tools/csnloc check
```

Compila si hace falta y corre los 7 tests en C de `csnloc`. **Este es el que debes usar cuando
modifiques el localizador.**

---

## 3. Sección: construir el repo de tanks (el paso que falta)

**Contexto:** `tank_repo/` hoy tiene **solo `test2`**. En `mseed/` hay decenas de miniSEED sin
convertir. Esto hay que hacerlo **una vez** y luego se reutiliza.

```bash
source ./ew8_unix.sh
./tank_tools/build_tank_repo.sh
```

**Qué hace:** recorre `mseed/*.mseed`, convierte cada uno a `tank_repo/<slug>/master.tank` y
escribe `tank_repo/<slug>/manifest.json` (span, inventario de estaciones, nº de mensajes). El
manifest es lo que después hace que `plan` sea instantáneo.

**Es dinámico e inteligente (no hay lista hardcodeada):**

- Descubre los ficheros con `find mseed/ -maxdepth 1 -type f -name '*.mseed' | sort`.
- Ignora cualquier cosa que no sea `*.mseed` (`.mpd`, `.txt`, etc.).
- Deriva el `slug` del propio nombre del fichero (`<nombre>.mseed` → `<nombre>`).
- **Reanudable:** si ya existe `<slug>/manifest.json`, lo salta (usa `--force` para rehacerlo).
- **Tolerante a fallos:** si un fichero sale desordenado, reintenta con `--remux`; si aun así
  falla, continúa con el resto y al final te lista los fallidos (sale con código `2`).

**Cuánto tarda / pesa:** ~2 min por fichero; ~**20-25 GB** en disco para el conjunto completo.

**Opciones:**

| Opción | Qué hace | Valores | Por defecto | Cuándo cambiarla |
|---|---|---|---|---|
| `--only GLOB` | Convierte **solo** los que casen con el patrón | un glob, p.ej. `'simulacion3_*'` | `*` (todos) | ⭐ Para probar uno concreto sin esperar horas |
| `--dry-run` | **No convierte nada**, solo muestra qué haría | — | off | ⭐ Antes de lanzar el grande, para ver la lista |
| `--force` | Rehace los que ya existían | — | off | Si cambió el miniSEED de origen |
| `--src DIR` | De dónde lee los miniSEED | ruta | `mseed/` | ⚙️ Rara vez |
| `--repo DIR` | Dónde escribe el repo | ruta | `tank_repo/` | ⚙️ Rara vez |
| `--nsamp N` | Muestras por bloque interno | 1…1008 | `1008` (máximo) | ❌ No tocar |
| `--no-remux` | **No** reordena el tank | — | off | ❌ **Nunca**: el tank debe ir ordenado |

**Qué verás por pantalla** (por cada fichero):

```
[repo] origen  : /home/seba/Dev/EW8_GUItools/mseed
[repo] ficheros: <N>                 ← lo calcula él, no está fijado
[repo] (1/<N>) BUILD <slug> <- <fichero>.mseed
       OK  <tamaño>  nmsgs=...  span=...s  remux=true
...
[repo] listo en ... s
       construidos: <n_ok>
       saltados   : <n_skip>
       fallidos   : <n_fail>
```

**Códigos de salida:** `0` ok · `1` error de uso · `2` alguno falló (te los lista) · `3` faltan
binarios/ficheros.

**Forma corta** (misma cosa, vía Makefile): `make -C tank_tools tank-repo`.

**Cómo saber si un tank quedó bien:**
`./tank_tools/tank_replay.sh sniff tank_repo/test2/master.tank` te muestra las estaciones y la
ventana temporal.

---

## 4. Sección: ejecutar mis pruebas con los tanks (el uso normal)

Este es el comando que usarás el 90 % del tiempo. Tiene dos fases: **planificar** (no ejecuta
nada) y **ejecutar**.

### 4.1 Planificar (siempre primero)

```bash
./tank_tools/run_events.sh plan tank_repo
```

**Qué hace:** lee los `manifest.json`, trocea la línea temporal en **sesiones** y te imprime el
plan. **No arranca nada.** Es tu forma de ver cuántas sesiones saldrán y de qué ventana antes de
gastar tiempo.

**Variantes:**

| Quiero… | Comando |
|---|---|
| Plan de **todos** los eventos | `./tank_tools/run_events.sh plan tank_repo` |
| Plan de **un solo** evento | `./tank_tools/run_events.sh plan tank_repo/test2` |
| Plan de **un tank suelto** | `./tank_tools/run_events.sh plan replay/tanks/master.tank` |

> **Ojo:** "un tank" no es "una sesión". Un tank largo (p.ej. `test2`) se parte en **varias
> sesiones**; el `plan` te dice cuántas.

### 4.2 Ejecutar

```bash
./tank_tools/run_events.sh run tank_repo/test2        # un solo tank (sus sesiones)
./tank_tools/run_events.sh run tank_repo              # TODOS, interactivo
./tank_tools/run_events.sh run tank_repo --yes        # TODOS, seguidos
```

**Qué hace:** para cada sesión levanta un EarthWorm **aislado** (copia de `params` en
`runs/<slug>/params`, logs propios), reproduce el tank con `tankplayer`, deja que `csnloc`
localice, y guarda todo en `runs/<slug>/<NNN>/`.

**⚠️ Aviso crítico:** el modo replay **DETIENE el EarthWorm que esté en marcha** y arranca con
`startstop_replay.d` (sin `slink2ew`). **No lo uses en operación.**

**Tres formas de "pasar los eventos" (uno a la vez vs todos juntos):**

| Situación | Cómo | Comportamiento |
|---|---|---|
| **Uno a la vez**, revisando cada uno | `run tank_repo` (default `--interactive`) | Se **detiene tras cada evento** y te espera |
| **Todos juntos**, sin parar | `run tank_repo --yes` (o `--chain`) | Encadena todos seguidos |
| **Un solo tank** | `run tank_repo/test2` | Solo ese (todas sus sesiones) |

**Opciones de `run` (todas las que existen, explicadas):**

| Opción | Qué hace | Valores | Def. | ¿La uso? |
|---|---|---|---|---|
| `--mode fast\|realtime` | `fast` = comprime el ruido y va rápido; `realtime` = reproduce al ritmo real (fiel) | `fast`/`realtime` | `fast` | ⭐ Cambia a `realtime` si necesitas fidelidad exacta (tarda mucho más) |
| `--chain` / `--interactive` | Directorio: todos seguidos / parar tras cada uno | — | `interactive` | ⭐ `--yes` es atajo de `--chain` |
| `--yes` | Atajo no interactivo (= `--chain`) | — | — | ⭐ |
| `--assoc-window S` | Ventana de asociación de `csnloc` en la copia aislada | segundos | `120` | ⭐ `40` = perfil de **cribado rápido** (sesiones ~40 s más cortas, menos exhaustivo) |
| `--keep-tanks` | No borra los `.tnk` de trabajo al acabar cada evento | — | off | ⭐ Para inspeccionar un fallo |
| `--outdir DIR` | Raíz de resultados | ruta | `runs` | ⚙️ |
| `--workdir DIR` | Dónde van los trozos `.tank` | ruta | `<outdir>/_work` | ⚙️ |
| `--chunk-span S` | Span de dato por sesión | ≥30 | `700` | ⚙️ Ver "por qué" abajo |
| `--overlap S` | Solape entre trozos | < chunk-span | `180` | ⚙️ |
| `--min-wall S` | Duración de pared objetivo en `fast` | seg | `70` | ⚙️ |
| `--settle S\|auto` | Asentamiento tras el replay | seg o `auto` | `auto` | ❌ auto = `assoc-window/2 + 1 + 4` |
| `--params-mode M` | Alineación de la copia de params | `hhz` | `hhz` | ❌ |
| `--chan CC` | Canal para el troceo | `HH`/`BH`… | `HH` | ❌ |
| `--sendlate S` | Cuánto se re-estampa hacia atrás | seg (<900) | `30` | ❌ Es lo que hace que `csnloc` no descarte los picks |
| `--startup S` | Retardo de arranque de `tankplayer` | seg | `6` | ❌ |
| `--pause S` | Pausa entre ficheros | seg | `0` | ❌ |
| `--timeout S` | Tope por sesión | seg | `900` | ❌ |
| `--no-debug` | No activa `Debug`/`DumpHypo` en la copia | — | off | ❌ El debug es lo que te deja ver la hora origen |
| `--force` | Repite sesiones ya hechas | — | off | ⭐ Tras cambiar tu código |
| `--no-cut` | Plan: no cortar trozos (estimación gruesa) | — | off | ⚙️ Solo para `plan` |
| `--dry-run` | Imprime lo que haría y sale | — | off | ⭐ Antes de una corrida larga |

**Por qué esos valores por defecto:**

| Valor | Motivo |
|---|---|
| `--chunk-span 700` | `wave_serverV` rechaza paquetes a más de 900 s en el futuro; con `SendLate 30` el margen seguro deja el corte en ~700-870 s |
| `--overlap 180` | Garantiza que un sismo a caballo entre dos trozos se vea completo en al menos uno |
| `--assoc-window 120` | `csnloc` asocia cada `ventana/2 + 1` s → con 120 necesita ~61 s de pared, por eso las sesiones no pueden ser más cortas |
| `--sendlate 30` | Re-estampa el dato histórico a "ahora"; sin esto `csnloc` descarta los picks por viejos y **nunca localiza** |

**Dónde quedan los resultados:**
`runs/<slug>/<NNN>/session.json` · `runs/<slug>/<NNN>/logs/` (csnloc, csnmags_toy,
wave_serverV…) · `runs/<slug>/<NNN>/play.log`

**Códigos de salida:** `0` ok · `1` error de uso · `2` alguna sesión falló · `3` faltan binarios.

---

## 5. Sección: ver los resultados

```bash
python3 -m api_report_locs.report --runs runs
```

**Qué hace:** lee los `session.json` y los logs de cada sesión y te saca, por evento: hipocentro,
nº de fases, RMS, gap, **ML/Mwp**, magnitud preferida y la **hora origen histórica** (reconstruida
restando el `offsetTime` del replay). Marca duplicados.

**Opciones:**

| Opción | Qué hace | Valores | Def. | ¿La uso? |
|---|---|---|---|---|
| `--runs DIR` | Dónde buscar resultados | ruta | `runs` | ⭐ |
| `--format` | Qué emitir | `table`/`csv`/`json`/`all` | `table` | ⭐ `all` = tabla + ficheros CSV y JSON |
| `--slug S` | Filtrar por evento (repetible) | slug | — | ⭐ Para mirar solo uno |
| `--out PREFIJO` | Prefijo de los ficheros de salida | ruta | `<runs>/report` | ⚙️ |
| `--catalog CSV` | CSV de referencia para comparar | columnas `slug,lat,lon` | — | ⭐ Si quieres validar contra el catálogo real |
| `--dup-tol-deg G` | Tolerancia para agrupar soluciones del mismo sismo | grados | `0.5` | ⚙️ |
| `--dup-tol-s S` | Ídem, en tiempo | seg | `300` | ⚙️ |
| `--quiet` | No imprimir la tabla | — | off | ⚙️ |

**Atajo:** `./tank_tools/run_events.sh report` hace lo mismo con los valores por defecto.

---

## 6. Sección: cuando algo falla

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| `csnloc` no localiza **nada** | Sesiones demasiado cortas para la ventana de asociación | Repite con `--assoc-window 40` (cribado rápido) |
| **No hay magnitudes** (ML/Mwp) | Canal **HHZ/BHZ** incoherente: `csnmags_toy` descarta fases **en silencio** | `./tank_tools/align_channels.sh --dry-run` → `--apply` → **reiniciar EarthWorm** |
| `wave_serverV` avisa de `.tnk` **huérfanos** al arrancar | Quedaron tanks del canal viejo | `./tank_tools/align_channels.sh --prune-orphans run_working_v8/tanks/_orphan_bhz --apply` |
| Log dice `fails validity check, discarding` | Paquete a >900 s en el futuro (modo `fast` con corte largo) | Baja `--chunk-span` (por defecto ya es seguro) |
| Quiero **depurar un solo tank** a mano | Necesitas ver la reproducción en vivo | `./tank_tools/tank_replay.sh play <tank> --dry-run` (genera el `.d` sin arrancar) y luego `play <tank> --mode realtime` |
| Un comando "no arranca" con error raro de `timeout` | `timeout` del sistema está ensombrecido | Usa la ruta absoluta `/usr/bin/timeout` |
| `sniffring` dice que el ring está vacío | Sin `-n` **drena** el ring | `earthworm_8.0/bin/sniffring -n HYPO_RING` |

**`align_channels.sh` en detalle** (es el que arregla el error más traicionero):

| Opción | Qué hace | Valores | Def. | ¿La uso? |
|---|---|---|---|---|
| (sin opciones) | **Dry-run**: solo informa qué cambiaría | — | — | ⭐ Siempre primero |
| `--apply` | Aplica, con respaldo `.bak.<fecha>` | — | off | ⭐ |
| `--prune-orphans DIR` | Mueve los `.tnk` huérfanos | ruta | — | ⭐ |
| `--mode hhz\|bhz` | A qué canal alinear | `hhz`/`bhz` | `hhz` | ❌ En este repo es `hhz` |
| `--only ...` | Limitar a un sitio (`estaciones`/`pickfp`/`tanks`) | — | todos | ⚙️ |
| `--params-dir DIR` | Directorio de params | ruta | el real | ⚙️ |

**`tank_replay.sh` (reproducción manual de un solo tank):** subcomandos `build` (convierte),
`sniff` (inventario), `play` (reproduce), `stop`, `status`. De `play` solo importan
`--mode realtime|fast`, `--sendlate S` (`none` = conservar timestamps históricos → `csnloc` **no**
localiza) y `--dry-run`.

---

## 7. Resumen: los únicos comandos que necesitas

```bash
# (una vez por sesión de trabajo)
source ./ew8_unix.sh

# (si modificaste tu localizador) probar TU módulo
make -C ew_gui_tools/csnloc check

# (una sola vez, pendiente) construir el repo de tanks
./tank_tools/build_tank_repo.sh

# (cada vez que quieras probar) planificar y ejecutar
./tank_tools/run_events.sh plan tank_repo
./tank_tools/run_events.sh run  tank_repo/test2     # un tank
./tank_tools/run_events.sh run  tank_repo --yes     # todos

# (ver resultados)
python3 -m api_report_locs.report --runs runs --format all
```

---

## 8. Validación rápida de `csnloc` (modo OFFLINE) — minutos, no horas

Este es el camino recomendado para **iterar sobre `csnloc`**. En vez de reproducir las
formas de onda en tiempo real (~10 h), se hace **una sola vez** la captura de picks y
luego cada validación cuesta **segundos**. Capturas y validaciones quedan en carpetas
**inmutables con marca de tiempo**, así que nada se pisa y puedes comparar corridas.

### 8.1 Cómo funciona

```
master.tank ──pick_FP (offline)──► picks/<ts>/<slug>.picks        (una vez, ~7 s/tank)
                                        │
                                        ▼
        csnloc <csnloc.d> <slug>.picks  (reloj virtual, sin anillos)
                                        │
                                        ▼
   picks/<ts>/csnlocvalidate_<ts>/<slug>.jsonl ──► tabla / comparación
```

- **`pick_FP` offline** lee el tank directo (sin `tankplayer`, sin rings, sin pauta).
- **`csnloc` offline** usa un **reloj virtual** (`now` = timestamp del pick) en vez de
  `time()`. Preserva los Δt y solo reemplaza el reloj de pared por el tiempo del dato,
  así que da **los mismos hipocentros** que en vivo, pero a velocidad de CPU.

### 8.2 Estructura de carpetas

```
picks/
├── 20260923-171530/                       # CAPTURA (inmutable)
│   ├── test2.picks
│   ├── simulacion5_RIquique20140403_mpd.picks
│   ├── csnlocvalidate_20260923-180012/    # VALIDACIÓN nº1 (csnloc v1)
│   │   ├── <slug>.jsonl   <slug>.log
│   │   ├── csnloc.d       copia de la config usada
│   │   └── manifest.json  metadatos (binario, config, nº tanks/soluciones)
│   └── csnlocvalidate_20260923-193045/    # VALIDACIÓN nº2 (csnloc v2)
└── 20260923-184501/                       # otra captura
```

### 8.3 Capturar los picks (una sola vez)

```bash
source ./ew8_unix.sh
./tank_tools/capture_picks.sh tank_repo picks        # -> picks/<marca de tiempo>/
```

**Qué hace:** por cada `tank_repo/<slug>/master.tank` corre `pick_FP` en modo offline y
guarda `picks/<marca de tiempo>/<slug>.picks`. Cada ejecución crea una carpeta **nueva**;
nunca sobrescribe una captura anterior.

**Opciones:** `--only GLOB` (subset) · `--out DIR` (carpeta exacta, reanuda) · `--name N`
· `--force` · `--dry-run`.

**Coste medido:** ~7 s por tank de ~300 MB; los 46 ≈ **~5-10 min**.

### 8.4 Validar (cada vez que toques `csnloc`)

```bash
make -C ew_gui_tools/csnloc                             # recompila TU localizador
./tank_tools/validate_csnloc.sh picks/20260923-171530   # <- la captura
```

**Qué hace:** por cada `<captura>/<slug>.picks` corre `csnloc <csnloc.d> <slug>.picks` y
escribe una carpeta **nueva** `<captura>/csnlocvalidate_<marca de tiempo>/` con los
`.jsonl`, los `.log`, una copia de `csnloc.d` y `manifest.json`. Al final imprime la tabla.

**Opciones:** `--no-report` · `--config FILE` · `--name N` · `--out DIR` · `--report-args "--format all"`.

**Coste:** **<1 s por evento** (los 46 en segundos).

### 8.5 Ver una corrida

```bash
python3 tank_tools/offline_report.py picks/20260923-171530/csnlocvalidate_20260923-180012
```

Agrupa las soluciones del **mismo evento dentro de cada tank** (la ventana deslizante
emite varias del mismo sismo) y muestra **una fila por evento**, con `n_sol` = cuántas
soluciones lo componen. El representante es la **última generada**.

### 8.6 Comparar dos corridas (progreso entre versiones de `csnloc`)

```bash
python3 tank_tools/offline_report.py \
    picks/20260923-171530/csnlocvalidate_20260923-180012 \
    picks/20260923-171530/csnlocvalidate_20260923-193045
```

Compara **tank contra tank** (nunca tanks distintos) y clasifica cada evento:

| Clase | Significado |
|---|---|
| `MISMO` | Δt ≤ T y Δd ≤ D |
| `MISMO_TIEMPO_AMPLIADO` | Δd ≤ D y T < Δt ≤ F·T |
| `TEMPORAL_SIN_ESPACIO` | Δt ≤ T y Δd > D |
| `SOLO_A` / `SOLO_B` | sin pareja en la otra corrida |

Si `csnloc` no cambió, los `.jsonl` son idénticos byte a byte y lo dice:
**`*** CORRIDAS IDENTICAS (sin cambios) ***`**.

**Parámetros:** `--time-window T` (30 s) · `--dist-deg D` (1.0°) · `--window-factor F` (2.0)
· `--format table|csv|json|all` · `--out PREFIJO`.

### 8.7 Atajos (Makefile)

```bash
make -C tank_tools capture                                   # TANKS=tank_repo PICKS=picks
make -C tank_tools validate CAPTURE=picks/<captura>
make -C tank_tools offline                                   # capture + validate
make -C tank_tools report-offline VALIDATION=<csnlocvalidate_*>
make -C tank_tools report-offline VALIDATION=<A> CONTRA=<B>  # comparación
```

### 8.8 Notas

- El `.jsonl` trae, por evento: `t0`, `lat`, `lon`, `depth_km`, `nphases`, `rms_sec`,
  `gap_deg`, `dmin_km`, `score` y las **fases** con su `residual`.
- No calcula magnitudes (ML/Mwp): eso necesita formas de onda.
- Test del modo offline: `source ./ew8_unix.sh && make -C ew_gui_tools/csnloc check-offline`.

---

## 9. Mantenimiento: config portable y coherencia de estaciones

Cuando lleves el sistema a otra máquina (pruebas en tiempo real con datos reales):

```bash
./deploy_portable.sh --dst ~/ew8portable --dry-run -v   # ver el plan
./deploy_portable.sh --dst ~/ew8portable                # copiar (incremental)
```

Copia solo lo necesario, con **rutas relativas** (`wave_serverV.d` se regenera con `../tanks/…`).
En el destino: `cd ew8portable && source ./ew8_unix.sh && startstop`.
Validación del árbol copiado: `./deploy_portable.sh --dst DIR --verify-only` (exit 2 si falla).
Su prueba offline vive en `tank_tools/tests/test_deploy_portable.sh` (dentro de `make test`).

Para que las magnitudes (ML/Mwp) usen todas las estaciones posibles, elimina los SCNL presentes en
`pick_FP.sta`/`wave_serverV.d` pero **ausentes** en `estaciones_107.txt`:

```bash
./tank_tools/prune_orphan_stations.sh --dry-run
./tank_tools/prune_orphan_stations.sh --only pick,tanks --apply
./tank_tools/align_channels.sh --only tanks \
    --prune-orphans run_working_v8/tanks/_orphan_removed --apply
```

`csnloc` localiza y `csnmags_toy` mide **solo** con estaciones de `estaciones_107.txt`; un pick sin
metadata se descarta en silencio.

