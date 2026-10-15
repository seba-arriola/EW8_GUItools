# PLAN — Worktrees de git para paralelizar el trabajo en EW8_GUItools

**Estado: PAUSADO / SIN IMPLEMENTAR.** Este documento es la especificación acordada antes de
pasar a `build`. No se ha escrito ni modificado código de producción.

Fecha del reconocimiento: 2026-10-14.

---

## 1. Objetivo y alcance

Usar `git worktree` para trabajar **en paralelo** (varias líneas de trabajo a la vez) sobre este
repo. Tras el reconocimiento y las decisiones del usuario, el alcance incluye:

- **Stack EarthWorm vivos en paralelo**, uno por worktree (no solo edición + compilación + tests
  headless/offline).
- Cada worktree con su propia instalación operativa, sus anillos y su BD.

Quedan fuera (ver §11): `mseed/`, `resources/`, `wave_serverV.d`, refactors no pedidos.

## 2. Decisiones tomadas (cerradas con el usuario)

| Tema | Decisión |
|---|---|
| Alcance | Editar + compilar + tests **y** stacks EarthWorm vivos simultáneos |
| `earthworm_8.0/` (1 GB, ignorado) | **Symlink-farm**: symlink por entrada al install base + `bin/` real con symlink por archivo |
| `AGENTS.md`, `docs/`, `opencode.jsonc`, `.opencode/` (untracked) | **Los commitea el usuario** antes de pasar a `build` |
| Tabla de keys desplazada | **EW_PARAMS propio por instancia** (`params_inst_<N>/` gitignored); sin tocar archivos tracked, sin `skip-worktree` |

## 3. Reconocimiento — hechos verificados

### 3.1 Portabilidad de rutas: excelente

Un worktree es un checkout del **árbol tracked** en otra ruta, y este repo deriva todo de la
ubicación del propio script, sin rutas absolutas hardcodeadas en código:

- `ew8_unix.sh:4` — `DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"`.
- `ew8_unix.sh:12-15` — `EW_HOME="${DIR}"`, `EW_VERSION="earthworm_8.0"`,
  `EW_PARAMS="${DIR}/run_working_v8/params"`, `EW_LOG="${DIR}/run_working_v8/log"`.
- `ew8_unix.sh:32` — `PATH="${DIR}/ew_gui_tools/common/tools/gnubin:${DIR}/${EW_VERSION}/bin:$PATH"`.
- `ew_gui_tools/common/Makefile:13` — `EW_HOME ?= $(abspath $(CURDIR)/../..)`.
- `ew_gui_tools/common/tools/diag.sh:44-48` — `ROOT` derivado de `$0`.
- `install_bin.sh` instala en `$(EW_HOME)/$(EW_VERSION)/bin` (los Makefiles pasan ese destino).

Únicas rutas absolutas relevantes, y **todas fuera del repo** (por tanto compartibles):
`SdsRoot /mnt/sds/archive`, `OverlayFile/ProfileDataDir/ResponseDir/CalibFile /mnt/d/resources/...`,
`WphaseHome/WphaseBin/GfDir /home/seba/Dev/wphase/...`.
Excepción: `run_working_v8/params/wave_serverV.d` tiene ~400 rutas absolutas al repo, pero es
**legado no desplegado** (`startstop_unix.d:89` lo tiene comentado).

### 3.2 Qué viaja y qué no

| Categoría | Estado | ¿Viaja al worktree? |
|---|---|---|
| `ew_gui_tools/`, `tank_tools/`, `api_report_locs/` (código) | tracked | ✅ |
| `run_working_v8/params/` (71 archivos: `.d`, `.sta`, `.crh`, `grids/`, JPGs 30 MB) | tracked | ✅ |
| `db/` (`schema.sql`, `docker-compose.yml`) | tracked (4) | ✅ |
| `earthworm_8.0/` (1 GB: headers, libs, binarios) | **ignorado** (`.gitignore:9`) | ❌ |
| `AGENTS.md`, `docs/`, `opencode.jsonc`, `.opencode/` | **untracked** | ❌ |
| `run_working_v8/params/db.env` | ignorado (`.gitignore:23`) | ❌ |
| `run_working_v8/{log,tanks,wstore}`, `mseed/`, `resources/`, `runs/`, `picks/`, `replay/` | ignorados | ❌ |
| WIP local (21 modificados + 10 untracked) | sin commitear | ❌ (el worktree sale de un commit) |

Un worktree nuevo trae ~755 archivos tracked y le falta toda la plataforma EarthWorm, los
no-versionados que exigen los tests, y el WIP.

### 3.3 Dependencias de tests con archivos untracked

Sin `AGENTS.md` y `docs/`, `make -C ew_gui_tools/common check` **falla**:

- `common/tests/test_launch_doc.sh:13,25` — exige `$ROOT/AGENTS.md`.
- `common/tests/test_schema_coverage.sh:19` — exige `docs/DATA-MAPPING.md`.
- `common/tests/test_agent_permissions.sh:383,385` — exige `docs/AGENTES-PERMISOS.md` y
  referencia `AGENTS.md`.

### 3.4 Habilitante clave: la resolución nombre→key es por archivo

- `earthworm_8.0/src/libsrc/util/getutil.c:110` —
  `static char *TableFile[] = {"earthworm_global.d","earthworm.d"};`.
- `getutil.c:543` — `paramdir = getenv("EW_PARAMS")`.
- `getutil.c:569-576` — arma `$EW_PARAMS/earthworm_global.d` y luego `$EW_PARAMS/earthworm.d`.
- `getutil.c:626` — línea `Ring <nombre> <key>`.
- `GetKey()` en `getutil.c:180` / `GetKeyCore` en `:141`.

**Ningún módulo hardcodea keys**: todos pasan *nombre* a `GetKey` + `tport_attach`
(`csnloc.c:175`, `pickS/config.c:161`, `csnmags.c:511`, `hypodb_ingest_main.c:97`,
`wstore_ingest.c:261`, `hyp2000_ring.c:501`, `nlloc_ring.c:932`, `csnhypodbp_main.c:631`,
`csnrv.c:1078`, `csnstaevdisp.c:257`, `csntvp_data.c:132`, `ew_controller_main.c:133`,
`ewgui_ring.c:10`). Único literal numérico: `common/tests/test_ctrl.c:69` (test).

Y los **semáforos derivan de la misma key** del anillo (`transport.c:210,367`), así que renumerar
`earthworm.d` aísla shm **y** semáforos.

`startstop` **crea** los anillos de su `.d` (`startstop_unix_generic.c:630,2136`) y **no** pasa
keys a los hijos (`:2086` `execvp`, el hijo hereda el entorno y resuelve `GetKey` solo).

### 3.5 Reglas que hay que respetar

- `earthworm.d:44-51` — `FLAG_RING` **no** se pone en `startstop*.d`; con dos `startstop` hay que
  cambiar **todas** las keys, "including this one".
- `tport_createFlag()` (`stopstop:2133`/`transport.c:1946`) y `tport_createNamedEventRing()`
  (`:2134`) usan keys por defecto (`SHM_FLAG_DEFAULT_KEY 9999`, `NAMED_EVENT_DEFAULT_KEY 9991`) si
  no están en el mapa ⇒ hay que fijarlas distintas por instancia.
- El lock de `startstop` es `EW_LOG/<config>.lock` (`lockfile.c:47-50`); como `EW_LOG` es por
  worktree, **no** hace falta renombrar el `.d` de startstop.
- Todo anillo usado debe estar en `startstop_unix.d:11-20` (`nRing 9`: `WAVE_RING`, `SCN_RING`,
  `PICK_RING`, `HYPO_RING`, `HYPO_RING_REF`, `SLINK_RING`, `DECI_RING`, `ALARM_RING`,
  `CONTROL_RING`), que solo da **nombres y tamaños**; las keys salen de `earthworm.d:29-45`.

## 4. Reparto real de colisiones entre worktrees

| Recurso | ¿Colisiona? | Evidencia |
|---|---|---|
| Keys de anillos + semáforos | **SÍ** → aislar | `getutil.c:626`, `transport.c:210` |
| `FLAG_RING` 2000, `NAMED_EVENT_RING` 9991 | **SÍ** → rekey | `earthworm.d:45`, `ew_nevent_message.c:28-30` |
| Contenedor/volumen/puerto BD (`ew8_hypodb`, `5435:5432`) | **SÍ** → aislar | `docker-compose.yml:13,18,23,25` |
| `pgrep -x` en `ew_monitor.sh` / `diag.sh` | **SÍ** → pidfiles | `ew_monitor.sh:49,583`; `diag.sh:288,325` |
| `/tmp/csnhypodbp_mww`, `/tmp/opencode/diag` | **SÍ** | `csnhypodbp.d:108`, `diag.sh:48` |
| `hyp2000_ring`/`nlloc_ring` `WorkDir` (`/mnt/d/resources/{hyp2000,nlloc}`, escriben temporales) | **riesgo** | `hyp2000_ring.d:19`, `nlloc_ring.d:24` |
| Puertos `WsPort 16022`, `16005`, `16001`, `18000` | **NO** | módulos son **cliente** (`csnmags/wave_source.c:171`); único `bind` es `wave_serverV`, comentado (`startstop_unix.d:89`) |
| `EW_PARAMS`/`EW_LOG`, locks, `.noexec_fifo`, consola, `wstore`, `*.state`, `*.ndx` | **NO** (derivan del worktree) | `ew8_unix.sh:14-15`, `lockfile.c:47-50`, `startstop_unix_generic.c:360` |
| `/mnt/d/resources`, `/mnt/sds/archive`, `GfDir` | **NO** (solo lectura) | `.d` absolutos compartidos |

## 5. Diseño — contrato de instancia

- `instance = 0` ⇒ comportamiento actual, **cero cambios** (retrocompatible).
- `instance = N ∈ [1..9]` ⇒ `offset = 10000·N` aplicado a **toda** key `Ring` de `earthworm.d`,
  incluidas `FLAG_RING` (2000+offset) y añadiendo `Ring NAMED_EVENT_RING 9991+offset`.
  Evita el rango base (1000–1060, 2000, 9991, 9999).
- `FLAG_RING`/`NAMED_EVENT_RING` no se listan en `startstop_*.d` (solo en el mapa).

### 5.1 `earthworm_8.0/` en el worktree (symlink-farm)

- `earthworm_8.0/` = directorio real con un **symlink por entrada** al install base
  (`include/`, `lib/`, `src/`, `devel/`, `params/`, …).
- `earthworm_8.0/bin/` = directorio **real**, con un **symlink por archivo** al `bin/` base.
  Así `install_bin.sh` (`cp` a temporal + `mv` atómico) **sustituye el symlink** del módulo que
  compilas sin pisar el `bin/` de otro worktree ni el base ⇒ aislado en binarios, ~0 GB extra.
- `EW_HOME` (derivado de `ew8_unix.sh`) apunta al worktree; `PATH` incluye su `bin/`.

### 5.2 `EW_PARAMS` por instancia

- `run_working_v8/params/` (**tracked**) queda **intacto** = plantilla de la instancia 0.
- Con `--instance N` (N≥1) se crea `run_working_v8/params_inst_<N>/` (**gitignored**, copia 76 MB)
  y se escribe ahí: `earthworm.d` rekeyed, `MwwWorkDir` de instancia, `db.env` con su `DB_PORT`.
- `ew8_unix.sh` resuelve `EW_PARAMS` con esta prioridad:
  1. `EWGUI_PARAMS_DIR` si está definido (override explícito);
  2. si existe `<DIR>/.ewgui-instance` → `run_working_v8/params_inst_<N>`;
  3. si no → `run_working_v8/params` (comportamiento actual).
- `diag.sh` pasa a `PARAMS="${DIAG_PARAMS:-${EW_PARAMS:-$ROOT/run_working_v8/params}}"` y
  `LOGDIR="${DIAG_LOG:-${EW_LOG:-$ROOT/run_working_v8/log}}"`.
- `ew_monitor.sh` **ya preserva** `EW_PARAMS`/`EW_LOG` del llamante (`ewmon_env_run`,
  `ew_monitor.sh:66-75`), así que hereda el dir de instancia sin cambios extra.

**Efectos colaterales (deseables):** los archivos de estado que hoy viven en `params/`
(`slink126.state`, `pick_FP_151.ndx`, `ew2openapi_144.queue`, `csnloc.events`,
`csnhypodbp_manual.events`, `hyp2000_output/`) pasan a ser **por instancia** automáticamente;
`StoreRoot ../wstore` sigue resolviendo a `run_working_v8/wstore` del worktree.

### 5.3 Herramientas y BD

- `ew_monitor.sh`: `$EW_LOG_DIR/startstop.pid` (aditivo; `get_startstop_pid()` lo prefiere,
  validado con `/proc/$pid/cmdline` vía `ewmon_proc_module`, `ew_monitor.sh:169-174`), con
  **fallback** a `pgrep -x` si falta o está corrupto. `ewmon_leftovers` (`:570-587`) deja de matar
  procesos del otro stack.
- `diag.sh`: `TMP="${DIAG_TMP:-$ROOT/run_working_v8/.diag}"` (por worktree) y `process`/`run`
  acotados a su `EW_PARAMS` (no `pgrep -x` global).
- `db/docker-compose.yml`: `name`/`container_name` → `${COMPOSE_PROJECT_NAME:-ew8_hypodb}` y
  `ports` → `"${DB_PORT:-5435}:5432"` (el volumen nombrado se aísla solo por proyecto).
  La BD se escribe de verdad (`hypodb_ingest.c:586-637`), de ahí el aislamiento.

## 6. Interfaz del script (contrato primero)

```
ew_gui_tools/common/tools/worktree_bootstrap.sh <worktree-path>
    [--instance N] [--ew-install DIR] [--db] [--refresh]
    [--print-plan] [--check] [--force]
```

- **Salida** 0 = worktree operativo; ≠0 con mensaje. Nunca escribe fuera de `<worktree-path>`.
- **Pasos**: (1) valida que `<wt>` es worktree de este repo (`git rev-parse --show-toplevel` +
  `.git` de fichero); (2) `earthworm_8.0/` symlink-farm + `bin/` real por archivo; (3) copia
  `db.env`; (4) crea `log/`, `tanks/`; (5) escribe `<wt>/.ewgui-instance` (`{instance, offset}`);
  (6) con `--instance N`: crea `params_inst_<N>/` y aplica overrides; (7) con `--db`: `db.env`
  con `DB_PORT` propio.
- **`--check`**: verifica layout y, con instancia, que las keys del `earthworm.d` de instancia y
  del base son **disjuntas** (y no repetidas entre instancias); ≠0 si hay solape o drift.
- **`--refresh`**: re-sincroniza `params_inst_<N>/` desde la plantilla **conservando**
  `earthworm.d`/`db.env` de instancia.
- **`--print-plan`**: dry-run. **Idempotente**; aborta sin `--force` si encontrara un archivo real
  donde debe symlinkear o un `earthworm.d` de instancia ya modificado.
- Herramienta auxiliar: `common/tools/ewgui_instance.py` con `rekey <earthworm.d> <offset>`.

## 7. Casos borde

| Caso | Comportamiento |
|---|---|
| Ruta que no es worktree | ≠0, "no es un worktree de este repo" |
| Falta el install base | ≠0 antes de escribir nada |
| Bootstrap repetido sin `--force` | ≠0; el resto es idempotente |
| Ya corre un stack del principal | aviso: `--instance 0` colisionaría; NO mata nada |
| `instance` ya usado por otro worktree | `--check` detecta solape de keys |
| `params_inst_<N>` ya existe | `--refresh` re-sincroniza; sin él no lo pisa |
| Borrado de `.ewgui-instance` | vuelve a instancia 0; `--check` lo reporta |
| `EW_PARAMS` exportado a mano al dir tracked | se respeta el env; `--check` avisa del riesgo |
| `git reset --hard`/`checkout` en el worktree | no afecta al rekey (vive en dir gitignored) |
| Sin `db.env` | aviso; solo `--db`/`hypodb` lo necesitan |

## 8. Criterios de aceptación

- **CA1** `diag.sh env` en el worktree (sin variables extra) → `EW_PARAMS` = dir de instancia,
  `EW_LOG` y `bin` dentro del worktree.
- **CA2** `make -C ew_gui_tools/common check` pasa en el worktree (implica `AGENTS.md`+`docs/`).
- **CA2bis** `--refresh` re-copia y **conserva** `earthworm.d`/`db.env` de instancia.
- **CA3** `make` de un módulo instala en `<wt>/earthworm_8.0/bin/<mod>` y **no altera** el
  binario/hash del principal.
- **CA4** segundo bootstrap = 0 cambios (idempotencia).
- **CA5** el script no escribe fuera de `<wt>` (hash de `earthworm_8.0` y `params/*.d` del
  principal invariantes).
- **CA6/CA6bis** `--check` de dos instancias: conjuntos de keys **disjuntos** y sin igualdad con
  el base.
- **CA7** `docker compose config` con `COMPOSE_PROJECT_NAME` distinto → nombre/puerto/volumen
  distintos.
- **CA8** con dos stacks arriba, `ew_monitor.sh status` en cada worktree reporta **solo sus**
  módulos; `stop` no toca los del otro.
- **CA9** ningún script versionado asume la ruta literal `run_working_v8/params`: usan
  `$EW_PARAMS` (test de invariante).

## 9. Fases y tareas

### Fase 1 — Bootstrap base (sin instancia)

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 1.1 | `common/tools/worktree_bootstrap.sh` | `<wt>` + `--print-plan`/`--check`/`--force`; valida worktree | `common/tests/test_worktree_bootstrap.sh` | layout creado; CA1, CA4, CA5 |
| 1.2 | idem | `earthworm_8.0` symlink-farm + `bin/` real por archivo | CA3 | aislado por binario |
| 1.3 | idem | copia `db.env`, crea `log/`+`tanks/`, escribe `.ewgui-instance` | CA2 | `common check` verde en el worktree |

### Fase 2 — Instancia de anillos

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 2.1 | `ew8_unix.sh`, `common/tools/diag.sh` | `EWGUI_PARAMS_DIR` + auto-detección de `.ewgui-instance`; `diag.sh` con default `$EW_PARAMS`/`$EW_LOG` | `test_launch_doc.sh` + `common/tests/test_instance_env.sh` | CA1, CA1bis |
| 2.2 | `common/tools/ewgui_instance.py` | `rekey <earthworm.d> <offset>` (keys + `NAMED_EVENT_RING`) | `common/tests/test_instance_keys.sh` | CA6bis |
| 2.3 | `common/tools/worktree_bootstrap.sh` + `.gitignore` | copia `params/`→`params_inst_<N>/`, aplica overrides, `--refresh`; ignora `params_inst_*/` | `test_worktree_bootstrap.sh` | CA2bis |
| 2.4 | scripts con ruta literal `run_working_v8/params` (barrido) | preferir `$EW_PARAMS` con fallback | `test_instance_env.sh` (grep de invariante) | CA9 |

### Fase 3 — Herramientas y BD por instancia

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 3.1 | `ew_monitor.sh` | pidfile `$EW_LOG_DIR/startstop.pid` + fallback `pgrep` validado | `common/tests/test_ew_monitor_module.sh` (extender) | CA8 |
| 3.2 | `common/tools/diag.sh` | `DIAG_TMP=<wt>/run_working_v8/.diag`; `process`/`run` acotados a su `EW_PARAMS` | `common/tests/test_diag_isolation.sh` | sin falsos "YA CORRE" |
| 3.3 | `db/docker-compose.yml` | `${COMPOSE_PROJECT_NAME:-…}` + `"${DB_PORT:-5435}:5432"` | `db/test_compose_isolation.sh` | CA7 |

### Fase 4 — Verificación end-to-end y documentación

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 4.1 | `common/tests/test_two_stacks.sh` (marca live, no en `check`) | crea 2 worktrees, bootstrap, valida keys disjuntas + `status` aislado | el propio test | CA6/CA8 en vivo |
| 4.2 | `docs/WORKTREES.md` + bullet en `AGENTS.md` | — | `test_launch_doc.sh` | flujo y límites documentados |

## 10. Prerequisitos antes de implementar

1. **Congelar el estado local**: commitear o descartar los 21 modificados + 10 untracked (un
   worktree sale de un commit).
2. **Commitear `AGENTS.md`, `docs/`, `opencode.jsonc`, `.opencode/`** (lo hace el usuario), para
   que los worktrees hereden lo que exigen los tests.

## 11. Fuera de alcance

- `mseed/`, `resources/`, `tank_repo/` (datos externos/enormes).
- `wave_serverV.d` (legado no desplegado; ~400 rutas absolutas al repo).
- Cualquier refactor no pedido.

## 12. Costes y riesgos

- Cada worktree con instancia: **+76 MB** de `params_inst_<N>/`.
- Cada worktree con stack vivo escribe **su propio** `run_working_v8/wstore` (~8.6 GB) ⇒ disco.
  Si no se quiere, es una decisión aparte (p. ej. store compartido de solo lectura para mirar).
- `hyp2000_ring`/`nlloc_ring` comparten `WorkDir` en `/mnt/d/resources/**` y escriben temporales:
  riesgo de choque si dos instancias refinan a la vez (ver §4). Pendiente de decidir si se
  parametriza por instancia.
- `HYPO_RING_REF2` (key 1017) no está en `startstop_unix.d`; `nlloc_ring` usa `HYPO_RING_REF` en
  su `.d` (`nlloc_ring.d:14`), así que no es bloqueante, pero conviene recordarlo.
- Renumerar keys **no** cambia los tamaños de anillo ni los nombres; solo las keys.

## 13. Nota de ejecución

`git worktree list` no estaba en la allowlist de shell del agente `plan` (denegado); se obtuvo por
la herramienta de OpenCode: hoy solo existe el worktree principal.
