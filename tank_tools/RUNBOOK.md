# Runbook — Pruebas de `csnloc` con tanks reales

Herramientas: `tank_tools/` (replay + runner) y `api_report_locs/` (reporte).
Objetivo: inyectar sismos reales (miniSEED) en la cadena `pick_FP → csnloc → HYPO_RING`
sin depender de SeedLink, y leer las localizaciones y magnitudes que produce.

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

> ⚠️ **`--out` vs el 2º argumento posicional (trampa frecuente).**
> El 2º argumento posicional es un **contenedor**: `capture_picks.sh tank_repo picks/20260925-084632`
> crea **otra** subcarpeta con marca de tiempo dentro (`picks/20260925-084632/<stamp>/`), no
> reanuda. Para **reanudar** una captura interrumpida (saltando los `.picks` ya hechos) hay que
> usar `--out` con la carpeta exacta:
>
> ```bash
> # MAL: crea picks/20260925-084632/20260925-124105/ (captura anidada nueva)
> ./tank_tools/capture_picks.sh tank_repo picks/20260925-084632
>
> # BIEN: reanuda sobre la carpeta exacta (salta los .picks existentes)
> ./tank_tools/capture_picks.sh tank_repo --out picks/20260925-084632
> ```
>
> Si ya quedó una captura anidada, se puede aplanar moviendo los `.picks` al padre:
> `mv picks/<cap>/<stamp>/*.picks picks/<cap>/ && rmdir picks/<cap>/<stamp>/`.
> `validate_csnloc.sh` solo mira el primer nivel (`find -maxdepth 1`), así que **no** valida
> subcarpetas.

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

### 8.7 Análisis de profundidad (sesgo de nodos)

`csnloc` nuclea en **nodos discretos** de profundidad y el refinamiento local solo puede mover
`z` dentro de una caja (`RefineDepthKm`). Para cuantificar cuánto pesa eso:

```bash
python3 tank_tools/depth_report.py picks/<cap>/csnlocvalidate_<ts>       # desde los .jsonl
python3 tank_tools/depth_report.py /home/seba/ew8portable/run_working_v8/log  # desde los logs
```

Reporta: histograma de profundidades, **offset al nodo de profundidad más cercano**
(media/máx y % dentro de 2.5/5/10 km), control de profundidad (`bien`/`aceptable`/`pobre`/
`sin_control`) y eventos clavados en el borde de una grilla. Con los logs reales medidos
(719 eventos) el **100 % quedaba a ≤5 km de un nodo** y ~91 % con control pobre o nulo.

`offline_report.py --depth` añade la columna `ctrl` a la tabla. El JSON de `csnloc` ya trae
`depth_ctrl`, así que ambos coinciden.

### 8.8 Atajos (Makefile)

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

Copia solo lo necesario, con **rutas relativas** (`wave_serverV.d`, `hyp2000_ring.d` y
`nlloc_ring.d` se regeneran con `../tanks/…` y `../../tmp/…`).
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

---

## 9. Picker S (`pickS`): calibración y barrido P+S para los localizadores

`pickS` publica picks S (`TYPE_PICK_SCNL`, 11.º token `S`) en `PICK_RING`; `csnloc`
los interpreta (`ew_gui_tools/csnloc/pick_scln.c:109-119`; sin token ⇒ P). La config
calibrada vive en `run_working_v8/params/pickS.d`:

    GuideMode       hybrid
    GuideMinDtSec   2.0      # ventana S-P (barrido sobre 410 eventos)
    GuideMaxDtSec   35.0
    GateIncidMinDeg 60.0     # compuerta dura de incidencia (la S es ~horizontal)
    GateIncidMaxDeg 120.0
    MinSnr          4.0
    MaxWeight       4
    MetricsLog      0        # observabilidad off en produccion

### 9.1 Resultado (410 eventos, hybrid, match por estación)

| estado | n | en_ref | pureza (cota inferior) |
|---|---:|---:|---:|
| antes (config previa) | 5074 | 1085 | 21.4 % |
| con la config calibrada | 1021 | 823 | 80.6 % |
| subconjunto certeza `ALTA` | 648 | — | 88.3 % |

`resid_S` mediana 1.16 s. `en_ref` es **cota inferior**: `picks_por_tests410.dat` es una
muestra gruesa (lo no presente es `SIN_REF`, no "FP"). El recall sigue bajo (FN ~10 338):
la calibración prioriza pureza. Discriminantes reales: **incidencia** y **ventana S-P**;
`snr`, `rectilinearity`, `planarity` y `hv` no discriminan.

### 9.2 Observabilidad y recalibración (no toca producción)

```bash
source ./ew8_unix.sh
./tank_tools/capture_picks_s.sh --ppicks picks tank_repo picks_s --metrics-out /tmp/m
python3 tank_tools/picks_s_manual.py report    --dat picks_por_tests410.dat \
    --tanks tank_repo --picks-s picks_s/<ts> --metrics /tmp/m \
    --events-tt <tt.tsv> --picksta run_working_v8/params/pickS.sta \
    --config run_working_v8/params/pickS.d --out /tmp/rep --format all
python3 tank_tools/picks_s_manual.py calibrate --dat picks_por_tests410.dat \
    --tanks tank_repo --metrics /tmp/m --events-tt <tt.tsv> \
    --config run_working_v8/params/pickS.d --out /tmp/cal.json
```

`MetricsLog 1` emite `pickS: METRICS …` por detección (canal lateral: no altera el pick,
el mensaje ni el anillo). Claves nuevas: `Q_Plan_W0/W4`, `Q_Hv_W0/W4` (0 = off),
`GateIncidMinDeg/MaxDeg` (Max≤Min = off), `MetricsLog`.

### 9.3 Barrido P+S offline y combinación para `csnloc`

En vivo no hace falta nada: `PICK_RING` ya trae P (`pick_FP`) y S (`pickS`). En OFFLINE
el modo offline de `csnloc` lee **un `.picks` por slug**, así que P y S deben combinarse:

```bash
# 1) barrido P+S sobre el repo de tanks (combinado idempotente)
make -C tank_tools capture-ps                 # -> picks_ps/<ts>/<slug>.picks
# o directo, reusando una captura P ya existente:
./tank_tools/capture_picks_ps.sh --pdir picks/20260925-084632 \
    --sdir picks_s/<ts> --out picks_ps/<ts> tank_repo

# 2) validar localizadores con P+S
make -C tank_tools offline-ps                 # capture-ps + validate-ps
./tank_tools/validate_csnloc.sh picks_ps/<ts>
python3 tank_tools/offline_report.py picks_ps/<ts>/csnlocvalidate_<ts>
```

`merge_picks.py` combina por slug de forma **idempotente**: reconoce los picks ya
presentes (clave SCNL + fase + tiempo) y los **omite**; la fase entra en la clave, así P y
S coexisten. Modos:

```bash
python3 tank_tools/merge_picks.py --out DIR --phases PS <dirP> <dirS>   # P+S
python3 tank_tools/merge_picks.py --out DIR --phases S  <dirP> <dirS>   # solo S
python3 tank_tools/merge_picks.py --out DIR --phases P  <dirP> <dirS>   # solo P
# --only GLOB, --force (reconstruye), --dry-run, --quiet, --selftest
```

`capture_picks_ps.sh` encadena `capture_picks.sh` (P) → `capture_picks_s.sh --ppicks`
(S guiado por las P) → `merge_picks.py`, y admite `--phases`, `--only`, `--force`,
`--dry-run`, `--metrics-out`, `--pdir/--sdir/--out`. **Reanudable**: los capturadores
saltan lo ya hecho y el merge no duplica.

### 9.4 Pendientes operativos

- Reiniciar EarthWorm para que `pickS` lea el `pickS.d` nuevo (lo lee al arrancar).
- Smoke test en vivo: `sniffring -n PICK_RING` tras el reinicio.

## 10. Mirar si los eventos del archivo aparecen (referencia, no métrica)

`tests_soluciones_publicadas.dat` (raíz del repo) trae, por evento, la solución publicada:

    N_test  origin_time  latitude  longitude  depth[km]  magnitude  magnitude_type

(TSV, cabecera + 410 filas). El slug de cada tank es `test<N_test>`. **Es una referencia para
mirar a ojo**, no una métrica absoluta: el localizador puede haber encontrado otras cosas y eso
no es "error".

Dos scripts, aplicados a cada corrida (cada set de parámetros):

### 10.1 `loc_report.py` — deja constancia de la corrida

Escribe `<validacion>/loc_report.txt` (texto plano) con cuatro bloques:

1. **Parámetros** del localizador: todas las claves de `csnloc.d` usadas, más las que no están en
   el `.d` (valor por defecto del código).
2. **Grillas**: nivel (global/regional/local), bbox, `NodeKm`, capas de profundidad y nº de nodos.
   Es la densidad, que es ajustable.
3. **Eventos localizados**: una fila por solución.
4. **Resumen**: conteo.

```bash
source ./ew8_unix.sh
python3 tank_tools/loc_report.py picks_ps/<ts>/csnlocvalidate_<ts>
# --out FILE (default <validacion>/loc_report.txt)   --selftest
```

### 10.2 `catalog_report.py` — el cruce con la referencia

Escribe `<validacion>/catalog_report.txt` con:

1. **Eventos del archivo**: una fila por evento del `.dat` con la solución localizada **más
   parecida** al lado y las diferencias (`d_km`, `dt_s`, `dz_km`); `(sin solucion)` si no hubo.
2. **Adicionales**: las otras soluciones de cada test (las que no son el evento del archivo),
   listadas como **plus a revisar**. No se penalizan.
3. **Resumen**: cuántos eventos del archivo aparecen, cuántos a ≤25/≤50/≤100 km, mediana del
   desvío y cuántos adicionales.

```bash
python3 tank_tools/catalog_report.py picks_ps/<ts>/csnlocvalidate_<ts> \
    --catalog tests_soluciones_publicadas.dat
# --time-window 30 --dist-deg 1.0 --bands 25,50,100 --out FILE --selftest
```

### 10.3 `calibrate_csnloc.py` — un set de parámetros por corrida

Materializa un `csnloc.d` **aislado** por variante (nunca toca
`run_working_v8/params/csnloc.d`), corre `validate_csnloc.sh` y deja, en `<work>/val/<tag>/`, el
`loc_report.txt` y el `catalog_report.txt` de ese set. Al final imprime una línea por set
(eventos del archivo encontrados a ≤25/50/100 km, mediana y adicionales).

```bash
python3 tank_tools/calibrate_csnloc.py --dry-run \
    --capture picks_ps/<ts> --catalog tests_soluciones_publicadas.dat --work tmp/calib
python3 tank_tools/calibrate_csnloc.py \
    --capture picks_ps/<ts> --catalog tests_soluciones_publicadas.dat \
    --work tmp/calib --jobs 8 [--only 'test[1-100]'] [--tags 'base,assoc*']
# atajo: make -C tank_tools calibrate CAPTURE_PS=... CALIB_ARGS="--jobs 8"
```

Variantes en `tank_tools/calib/csnloc_variants.json` (`{"tag": ..., "overrides": {clave: valor}}`);
`--tags` filtra por glob. Coste: una variante son 408 tanks ≈ 25-30 min.

### 10.4 Resultados

Registro completo en **`tank_tools/calib/RESULTS.md`**. Con el criterio de "cuántos eventos del
archivo aparecen y qué tan parecidos", el set que más le apunta es **`AssocWindowSec 45`**:

| set | ≤25 km | ≤50 km | ≤100 km | sin solución | mediana (km) | adicionales |
|---|---:|---:|---:|---:|---:|---:|
| base (120) | 164 | 299 | 340 | 38 | 27.6 | 742 |
| **assoc45** | **236** | **356** | **377** | **22** | **21.8** | 1389 |
| assoc60 | 217 | 344 | 371 | 25 | 23.0 | 1085 |
| assoc60 + `MinPhasesPerEvent 4` | 215 | 342 | 367 | 29 | 23.0 | **874** |
| assoc240 | 169 | 292 | 336 | 23 | 27.9 | 516 |

El óptimo está en ~45 s (por debajo, 30 s, aparecen casi los mismos pero se disparan las
soluciones; `assoc90` ya es peor que el base). Los adicionales son el precio de la ventana corta:
son un plus a revisar, no un defecto. Si el volumen importa, `AssocWindowSec 60` +
`MinPhasesPerEvent 4` es el mejor compromiso.

### 10.5 Notas

- Los `.jsonl` de una misma captura son **deterministas**: los reportes se pueden regenerar sin
  re-validar (`loc_report.py <val>` y `catalog_report.py <val>` sobre la misma carpeta).
- El `--dry-run` y el barrido real **no** escriben fuera de `--work`; el test
  `tank_tools/tests/test_catalog_report.sh` lo verifica comparando el sha256 de `csnloc.d`.
- Chequeo de datos: `test2.picks` es rancio (picks del Illapel 2015 en vez del evento
  2026-03-02); `test226` difiere 1 día (frontera UTC). 2 de 408.
- Pendiente (Fase 3): encadenar `hyp2000_ring`/`nlloc_ring` sobre los ARC de `csnloc`.
  **Hecho: ver §11.**

## 11. Calibrar los refinadores (hyp2000_ring / nlloc_ring)

Los refinadores consumen el **ARC crudo** que publica `csnloc` y escriben un ARC refinado. El bucle
es el mismo que el de `csnloc`, pero el insumo no son picks sino ARC.

### 11.1 El suelo (una vez)

`refine_arcs.py --baseline` corre `csnloc` OFFLINE con `DumpHypo 1` sobre una captura de picks,
extrae los `HYP2000ARC` del log y los congela:

    <out>/arcs/<slug>_<NN>.arc     el crudo (insumo identico de todas las variantes)
    <out>/<slug>.jsonl             el mismo crudo como soluciones (para --baseline)
    <out>/manifest.json            captura, sha256 del csnloc.d, nº de ARC

```bash
source ./ew8_unix.sh
make -C tank_tools refine-baseline            # usa la captura mas nueva de picks_ps/
# o: python3 tank_tools/refine_arcs.py --baseline --picks-dir picks_ps/<ts> --out tmp/refine
```

Coste: el mismo que `validate_csnloc.sh` (una corrida de `csnloc` por tank; ~25 min para 408).
Verificacion: el numero de ARC debe coincidir con el de soluciones del barrido de `csnloc`.

### 11.2 Barrido por refinador

```bash
python3 tank_tools/calibrate_refiners.py --refiner hyp2000 \
    --baseline tmp/refine --catalog tests_soluciones_publicadas.dat \
    --work tmp/refine/hyp2000 --jobs 8 [--only 'test[1-40]'] [--tags 'h_*']
python3 tank_tools/calibrate_refiners.py --refiner nlloc ... --jobs 9 [--arc-jobs 3]
# atajos: make -C tank_tools calibrate-refiners REFINER=hyp2000 REFINE_ARGS="--jobs 8"
```

Deja, por variante, en `<work>/val/<tag>/`:

- `refine_report.txt` — claves del `.d` + el `.hyp`/plantilla, y **todos** los ARC con el crudo al
  lado y las diferencias (`dz`, `dd`, `drms`).
- `catalog_report.txt` — el cruce con el catálogo **con el crudo al lado** (`base_d_km`, `base_z`).
- `arcs_ref/` y `<slug>.jsonl` — el ARC refinado y sus soluciones.

Y al final una línea por variante. Los sets viven en `tank_tools/calib/hyp2000_variants.json` y
`nlloc_variants.json`:

```json
{ "tag": "h_husen",   "hyp":  { "model": "husen" }, "d": { "MinPhases": 3 } }
{ "tag": "n_vpvs173", "ctrl": { "LOCMETH": "EDT_OT_WT 9999.0 4 -1 -1 1.73 6 -1.0 1" } }
```

- `hyp.model` elige el bloque `CRH`/`MUL`/`NOD` (`husen` | `ak135` | `bandas`); `hyp.scalars` pisa
  líneas sueltas del `.hyp` (`ZTR`, `DAM`, `MUL`, …).
- `d` pisa claves del `.d` (`MinPhases`, `MaxRMS`, `NllIntervalSec`, `bands: false` para el
  fallback 1D de nlloc, …).
- `ctrl` pisa líneas de la **plantilla de control de NLLoc** (`LOCSEARCH`, `LOCMETH`, `LOCGAU`, …),
  que el `.d` no expone: el arnés escribe una copia por variante.

Cada variante usa un **WorkDir aislado** (el refiner hace `chdir` ahí) y **rutas absolutas**
(relativas se resuelven contra el cwd equivocado tras el `chdir`). Con `--arc-jobs N` se replica el
WorkDir por worker para correr ARC en paralelo.

Coste medido (1777 ARC): **hyp2000 ~20 ms/ARC** (≈35 s por variante); **nlloc ~7 s/ARC** (≈3,5 h
por variante: dominan las tablas 3D de tiempos por estación; escalar `LOCGRID` no cambia ni el
resultado ni el tiempo). Para nlloc, cribar en un subconjunto (`--only`) antes de la corrida
completa.

### 11.3 Resultados

Registro en **`tank_tools/calib/RESULTS_REFINERS.md`**; inventario de parámetros (qué se tocó y qué
no, con el valor de producción) en **`tank_tools/calib/PARAMETROS.md`**.

Se decide con **tres familias de métricas**, no una: epicentral (≤25/≤50/≤100 km y mediana),
**profundidad contra la publicada** (`|dz|`) y RMS. Mirar sólo el epicentro da conclusiones falsas.

- `hyp2000_ring`: mejora con cualquier modelo razonable (251→~300 a ≤25 km, mediana 19.5→~12). El
  **1D único** gana el epicentro y el RMS; las **6 bandas 3D** ganan la **profundidad** (13.3 vs
  16.5 km). Los ejes que mueven son `DAM` y `ZTR` (nunca tocados antes), no el modelo.
- `nlloc_ring`: es el mejor de los tres (7.3 km de mediana frente a 11.9 de hyp2000 y 17.9 del
  crudo); el fallback 1D es inservible. Tenía un defecto de selección de banda que descartaba ~15 %
  de los eventos en silencio — corregido.

### 11.4 Notas

- **`nlloc_ring` elegía la primera banda cuyo bbox contiene el epicentro**, sin mirar si esa banda
  tiene las estaciones del evento. Como las bandas se solapan ~4° y las grillas sólo incluyen las
  estaciones del bbox±0.5°, un evento en el borde podía quedarse sin tiempos de viaje y descartarse
  en silencio. Ahora gana la banda que **cubre más estaciones** (`select_model`, `band_covers`).
- El modo OFFLINE de `hyp2000_ring` **no** aplica `MinPhases`/`MaxRMS` (están en el bucle de anillo,
  `hyp2000_ring.c:508-528`); `nlloc_ring` sí (`locate_arc`, `nlloc_ring.c:455-457`). El arnés emula
  el filtro sobre el ARC de entrada, como hace el anillo.
- **`POS` (Vp/Vs) no tiene efecto en hyp2000** con estos modelos: el `.crh` es sólo-Vp
  (`hycrh.for:14`, `FORMAT (2F5.2)`), así que no hay columna de Vs que controlar. Además
  HYPOINVERSE lo consume al leer `CRH`, así que el arnés lo emite **antes** del bloque de modelo.
- El WorkDir real del refiner es `resources/hyp2000/` (contiene `ak135.crh` y los `.crh` de banda). Si
  un `CRH` apunta a un modelo ausente de ahí, falla en silencio.
- Test de regresión: `tank_tools/tests/test_calibrate_refiners.sh` (selftests, `--dry-run` que no
  escribe, smoke real por refinador y sha256 de los `.d`/`.hyp`/plantillas de producción).

### 11.5 Modelo fusionado de todo Chile (`CHILE_4k`) y `LocGridKm`

Las 6 bandas 3D se solapan ~4° y las grillas de tiempos de cada una sólo traen las estaciones de su
bbox ± 0.5°, así que un evento con estaciones repartidas en más de una banda quedaba con **cobertura
parcial (33 %) o nula (~10 %)** y NLLoc no converge sin observaciones. Arreglo en dos piezas:

```bash
# 1) modelo fusionado (una vez, ~1 min): los 6 trozos de 4 km en una grilla comun
python3 ew_gui_tools/nlloc_ring/mk_mod_merge.py \
    --out resources/nlloc/mod3d/CHILE_4k/CHILE_4k.P.mod \
    --origin -33.0 -70.0 --step 4.0 --pad 3.0 \
    --bbox -45.90 -17.98 -75.90 -64.46 \
    --band /mnt/d/nll/time/N18-26_4k/N18-26_4k.P.mod --band ... --check

# 2) grillas de tiempos para TODAS las estaciones que caen dentro (108, ~20 min, ~12 GB)
python3 ew_gui_tools/nlloc_ring/mk_nll_grids_3d.py \
    --mod-dir resources/nlloc/mod3d --outdir resources/nlloc/time3d \
    --ctrl-dir resources/nlloc/ctrl --estaciones run_working_v8/params/estaciones_107.txt \
    --band CHILE_4k --margin 0.5 --grid2time ew_gui_tools/nlloc_ring/nlloc/Grid2Time --run
```

Se añade como **última** `ModelBand` de `nlloc_ring.d`: a igual cobertura gana la primera de la
lista, así que las bandas finas (1.5 km) mandan cuando cubren el evento entero y la fusionada (4 km)
sólo entra cuando ninguna lo logra.

Y `LocGridKm` (default 0 = off, en producción **600**): NLLoc **no acepta hipocentro semilla**, así
que el volumen de búsqueda es lo único que le dice dónde mirar. Con el LOCGRID de todo Chile
(409×945×80) el octree **no converge**; con cualquier volumen menor converge y da la misma solución.
El módulo re-centra el `LOCGRID` en el epicentro de entrada (600 km, recortado al modelo), lo que
además quita el pegado al borde de las bandas finas.

**Portabilidad**: las grillas de `CHILE_4k` (~12 GB) no las copia `deploy_portable.sh`; van a mano
como el resto de `tmp/`.

## 12. Grillas de nucleacion de `csnloc`

Las 12 grillas (`grids/*.grid`) son el **primer paso** de la localizacion: la nucleacion por
retroproyeccion, antes del refinamiento. Se generan con `mk_csnloc_grids.py` (antes eran ficheros
hechos a mano, copiados de la tiling de Glass3, sin generador ni forma de comparar alternativas).

```bash
python3 tank_tools/mk_csnloc_grids.py --tiling A \
    --out-dir tmp/csnloc_grids/A --check
```

- `--tiling current` reproduce produccion (sirve de validacion del script).
- `--tiling reg31` = locales de produccion + la regional nueva (aisla ese cambio).
- `--tiling A` = locales ensanchadas + la regional nueva.
- `--check` reporta nodos vs `MaxNodes` y la cobertura geometrica: celdas de 0.5° sin cubrir por
  ninguna local y **regresiones** contra las locales de produccion (debe ser 0).
- `--node-km` / `--local-max-nodes` para las variantes de resolucion.

**OJO**: `MaxNodes` no recorta, **aborta el arranque** (`grid.c:127-130` → `csnloc.c:652-654`).
Bajar `NodeKm` de 10 a 5 cuadruplica los nodos (una local pasa de ~85 k a ~330-760 k).

Para barrer una tiling, la variante del JSON lleva `grids_dir` y `calibrate_csnloc.py` reescribe el
**bloque** de grillas (las 10 lineas `LocalGrid` se repiten, asi que reemplazar por clave dejaria 9
apuntando a produccion):

```json
{ "tag": "g_A", "grids_dir": "tmp/csnloc_grids/A", "overrides": {} }
```

### 12.1 Resultado de la primera evaluación (408 tanks, 388 eventos)

| set | ≤25 km | ≤50 km | ≤100 km | mediana |
|---|---:|---:|---:|---:|
| base (producción, dedup 100) | 236 | 356 | 377 | 21.8 |
| **`EventDedupKm 50`** | **258** | **368** | **379** | **20.4** |
| `reg31` (sólo la regional nueva) | 227 | 356 | 377 | 22.4 |
| `A` (tiling A) | 221 | 338 | 368 | 22.6 |

**Más cobertura EMPEORA**: −15 con la tiling A, −9 sólo con la regional nueva (31 capas, más ancha)
y −5 bajando `NodeKm` a 5. Más cobertura = más candidatos y la etapa de selección (dedup + "gana la
grilla más fina") no los distingue: **diluye**. El criterio geométrico (0 regresiones, 100 % del pie
de Potin) se cumple pero no alcanza.

Lo que sí ganó, y está aplicado, es **`EventDedupKm 50.0`** en `csnloc.d` (antes corría a default
100): +22 eventos a ≤25 km y mediana −1.4 km.

### 12.2 Cerrado: ninguna tiling ni estrictez mejora las grillas actuales

Se probaron las dos direcciones, con `MinPhases`/`BackProjThreshold` **por grilla** (claves que
`grid.c` lee) y con `NodeKm` más fino. Todas empeoran sobre el baseline (`EventDedupKm 50` =
258/368/379, mediana 20.4):

| dirección | set | ≤25 km |
|---|---|---:|
| más cobertura | `A` | 236 |
| sólo la regional nueva | `reg31` | 227 |
| más estricto | `MinPhases 5` (cajas actuales) | 252 |
| más estricto | `BackProjThreshold 0.7` | 231 |
| más fino | 5 km | 64 (subconjunto) |

`g_cur_mp5` lo confirma evento a evento: el baseline encuentra 177 eventos que él no, y él 44 que el
baseline no → los candidatos que la estrictez elimina son **reales**.

**Conclusión**: en esta tubería *cualquier* cambio del conjunto de candidatos empeora, en las dos
direcciones; las grillas de producción están en un **óptimo local**. Cerrar el hueco de
frontera/fosa exige cambiar la **selección** (algoritmo, no parámetro). Detalle: `calib/RESULTS.md`
§9.1-9.2.


