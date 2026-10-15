# PLAN — Worktrees de git para paralelizar el trabajo en EW8_GUItools

**Estado: preparación HECHA (Fase 0 + Fase A, 2026-10-15); falta el bootstrap (Fase B).**
Este documento es el plan de registro: ✅ hecho · ⏳ pendiente.

---

## 1. Lo que ya está hecho

### 1.1 Repo limpio y `ew_8.0/` vendorizado ✅

- `earthworm_8.0/` (1004 MB) salió del repo; en su lugar hay **`ew_8.0/` (8,2 MB) versionado**:
  `include/`, `lib/{libew_mt.a,libmseed2.a,rw_mag.o}` y los 8 binarios de fábrica
  (`startstop`, `status`, `restart`, `stopmodule`, `slink2ew`, `pick_FP`, `sniffring`, `putpick`).
  Ver `ew_8.0/VENDORED.md`. **Un worktree nuevo ya trae EarthWorm**: no hace falta ningún
  *symlink-farm*.
- `EW_VERSION=ew_8.0`; `src/` ya no se usa (`csnstaevdisp` enlaza `libew_mt.a`).
- Fuera del repo: `tank_tools/`, `mseed/`, `picks/*`, `replay/`, `run_working_v8/tanks/`,
  `api_report_locs/`, `wave_serverV` (y su **backend TCP**, eliminado del código: `WSRC_WS`/
  `WSRC_AUTO`, `ws_get`, `WsIP/WsPort/WsTimeout/WaveBackend`, `SocketSysInit`).
- `params/`: se trackean `iasp91.tbl`/`.hed`; fuera los huérfanos (`ew_linux.bash`, `response/`,
  `startstop.desc`, `slink2ew.desc`, `ncal.hyp`, `hyp2000_output/`, `csnhypodbp_hist.txt`).

### 1.2 Fase 0 — el repo compila, pasa los tests y levanta el stack SIN `earthworm_8.0/` ✅

| comprobación | resultado |
|---|---|
| `common` + 13 módulos (`clean` + `make`) | `rc=0` |
| `common check` + `wavestore/csnmags/csnwphase/hypodb/csnloc/pickS check` + `csnhypodbp test` + `ew_controller test` | `rc=0` |
| `check_install.sh -v` | 16/16 OK |
| `./ew_monitor.sh start` | **15/15 `Alive`** |
| **V6 `cwd`** de los hijos de `startstop` | `csnloc`/`csnmags`/`csnhypodbp`/`wstore_ingest` → **`$EW_PARAMS`** |
| `stop` | limpio |

V6 es el dato que hace viable todo lo relativo: el cwd base de los módulos es `$EW_PARAMS`.

### 1.3 Fase A — `params/` = SOLO configuración; el runtime va a la carpeta de cada módulo ✅

Estructura resultante (todo lo no versionado, ignorado en git):

```
run_working_v8/
├── params/          SOLO config versionada (+ 2 salvedades de binarios de FÁBRICA)
├── log/             logs de logit + csnloc.events
├── wstore/          store indexado (wstore_ingest)
├── hyp2000_ring/    arcIn, arcOut                       (lo crea el módulo)
├── nlloc_ring/      obs.nll, nll_ring.in, loc/, last.*   (lo crea el módulo)
└── csnhypodbp/      csnhypodbp_manual.events, mww/       (lo crea la GUI)
```

Cambios:

| Pieza | Antes | Ahora |
|---|---|---|
| `hyp2000_ring.d` | `WorkDir /mnt/d/resources/hyp2000`, `CommandFile hyp2000_ring.hyp` | `WorkDir ../hyp2000_ring`, `CommandFile ../params/hyp2000_ring.hyp` |
| `hyp2000_ring.hyp` | `STA 'estaciones_hyp.sta'`, `CRH n 'Nxx.crh'` | `STA '../params/…'`, `CRH n '../params/…'` (el motor corre con cwd = `WorkDir`) |
| `nlloc_ring.d` | `WorkDir /mnt/d/resources/nlloc` + bandas/globales relativas al `WorkDir` | `WorkDir ../nlloc_ring`; `ModelBand` (`TtimeRoot`/`ControlFile`) y `ControlFile`/`TtimeRoot`/`ModelFallback` **absolutos y de sólo lectura** a `/mnt/d/resources/nlloc/…` |
| `csnhypodbp.d` | `MwwWorkDir /tmp/csnhypodbp_mww`, `ManualStateFile csnhypodbp_manual.events` | `../csnhypodbp/mww`, `../csnhypodbp/csnhypodbp_manual.events` |
| código | — | `mkdir -p` estático en `hyp2000_ring.c`/`nlloc_ring.c` (su `WorkDir`, y `loc/` en nlloc) y en `csnhypodbp_locate.c` (carpeta de estado) |
| `.gitignore` | reglas literales | los 3 dirs de módulo + bloque §5 con **patrones** (`slink*.state`, `pick_FP_*.ndx`, `*.tmp`, `*.bak*`) y sin las 4 reglas muertas |

**Validación de Fase A** (evidencia):

| prueba | resultado |
|---|---|
| **Golden** del ARC refinado, config HEAD vs nueva (arnés `locate_chain`, ARC real) | **IDÉNTICO** en los dos refinadores (`arcOut` 24 líneas / 1526 B; `ev.sum.grid0.loc.arc` 12 líneas / 1315 B) |
| **CA escrituras externas** (con **centinela** `find -newer`) tras una corrida nueva de nlloc `rc=0` | `/mnt/d/resources/nlloc`, `/mnt/d/resources/hyp2000`, `/mnt/sds/archive`, `params/` → **las cuatro VACÍAS** |
| Carpetas de los módulos | `hyp2000_ring/{arcIn,arcOut}`, `nlloc_ring/{obs.nll,nll_ring.in,loc/ev.*}` frescos |
| Regresión | `common check` `rc=0` (incluye el guardián nuevo) |
| **Guardián** `common/tests/test_params_stays_clean.sh` | `PASS=6 FAIL=0` (con 4 `WARN` legítimos: `/mnt/sds/archive` no está montado) |

**Las 2 únicas salvedades** (ambas de binarios de **fábrica**, sin clave de ruta en su `.d`):
`slink2ew` → `slink*.state` y `pick_FP` → `pick_FP_*.ndx`. Más `<mod>.d{.bak,.tmp}` si un humano
pulsa *Save* en `ew_controller` (es su función).

### 1.4 Base de datos por instancia (opción B) ✅

`db/docker-compose.yml` interpola `${COMPOSE_PROJECT_NAME}`/`${DB_PORT}`/`${DB_RESTART}` y
`ew8_db.sh` acepta `-i N` (o `EWGUI_INSTANCE`): `./ew8_db.sh -i 2 up` levanta `ew8_hypodb_2` en
`5437`, con contenedor, red y volumen propios y `restart: no`; aborta si el `db.env` de ese
worktree no declara el mismo `DB_PORT`. La instancia 0 es idéntica a siempre.

---

## 2. Estado del repo: qué hereda un worktree nuevo

| Categoría | Estado |
|---|---|
| `ew_8.0/`, `ew_gui_tools/`, `db/`, `docs/`, `run_working_v8/params/` (config) | tracked ✅ |
| `run_working_v8/{log,wstore,hyp2000_ring,nlloc_ring,csnhypodbp}` | ignorados (runtime, se crean solos) |
| `run_working_v8/params/db.env` | ignorado (se copia de `.example`) |
| Binarios de los módulos en `ew_8.0/bin` | ignorados (los pone `make install`) |
| Recursos pesados (`/mnt/d/resources/…`, `/mnt/sds/archive`, GF de fase W) | **fuera del repo**, de sólo lectura, referenciados por ruta absoluta en los `.d` |
| `git status` en cualquier worktree | limpio (nada trackeado cambia por ser worktree) |

---

## 3. Lo que queda: **Fase B (bootstrap)**

### B.1 `earthworm.d` generado (opción G)

- `run_working_v8/params/earthworm.d.base` (**tracked**, offset 0) con sentinelas que delimitan la
  sección de anillos y un encabezado «NO edites `earthworm.d`: es generado».
- `run_working_v8/params/earthworm.d` (**ignorado**, runtime): copia verbatim del `.base` con las
  keys de la sección de anillos `+10000·N`. Reglas: se excluyen `MOD_*`/`TYPE_*` (ids de módulo y
  de mensaje) e `INST_*` (los inst id son `unsigned char`); si el nº de líneas transformadas no
  coincide con el del `.base`, **aborta**; idempotente (siempre desde el `.base`).
- **N** se deriva del propio generado (`offset/10000`) y se cruza con `db.env`
  (`DB_PORT − 5435`); si no coinciden, aborta. El drift (`earthworm.d` ≠ base + offset) se detecta
  y se reporta.

### B.2 `ew_gui_tools/common/tools/worktree_bootstrap.sh`

```
worktree_bootstrap.sh [<wt>] [--instance N] [--check] [--refresh] [--print-plan] [--force]
exit 0 OK · 2 uso · 3 entorno · 4 --check con problemas
```

1. Valida que es un worktree de este repo (o el principal) y que `params/` es suyo.
2. Genera `earthworm.d` (B.1).
3. `db.env`: si falta, lo copia del worktree principal y forzar `DB_PORT=5435+N`; si existe,
   verifica que el puerto cuadre con N.
4. `--check` (no escribe): drift del generado · offsets **disjuntos** entre los `earthworm.d` de
   todos los worktrees (`git worktree list`) · `db.env` ↔ offset coherentes · todas las rutas
   relativas de las `.d`/`.hyp` resuelven · puerto libre · ningún proceso vivo con nuestro
   `EW_PARAMS` o nuestras keys (`/proc/*/environ`, `ipcs -m`).
5. `--refresh`: regenera y reporta drift. **Nunca escribe fuera del worktree.**
6. **No crea work dirs ni symlinks**: ya no hacen falta (Fase A).

### B.3 Aislar las herramientas compartidas

- **`ew_monitor.sh`**: `startstop.pid` en `$EW_LOG`; `get_startstop_pid` = pidfile validado por
  `/proc/<pid>/environ` → si no, `pgrep -x startstop` **filtrado** por `EW_PARAMS`, y **si hay >1
  aborta listando** (hoy toma el primero, `ew_monitor.sh:49`). `stop`/`ewmon_leftovers` sólo con
  nuestros pids (hoy mata por nombre en todo el sistema). Ojo: `start()` lanza `startstop` con cwd
  = raíz del repo (`:543`), el cwd de los hijos lo fija `startstop` (V6).
- **`diag.sh`**: `TMP="${DIAG_TMP:-<root>/run_working_v8/.diag}"` (hoy `/tmp/opencode/diag`, fijo) y
  `PARAMS`/`LOGDIR` por instancia; `process`/`run` filtran por `/proc/<pid>/environ` (hoy un
  worktree da falsos `YA CORRE`).

### B.4 Documentación y validación de dos stacks

- `docs/WORKTREES.md`: el procedimiento paso a paso (crear carpeta/rama, `git worktree add`,
  bootstrap, compilar, `./ew8_db.sh -i N up`, `start`/`status`/`stop`, borrar).
- CA con **dos stacks vivos a la vez**: keys disjuntas (`ipcs -m`), `status` de cada uno lista
  **sólo** sus módulos, `stop` en uno **no** toca el otro, dos contenedores de BD
  (`5435`/`5435+N`), sin cross-talk de picks (`putpick` + `sniffring` en cada `EW_PARAMS`),
  `time3d` sin escrituras nuevas, 5 GUIs por stack (`G_APPLICATION_NON_UNIQUE` ya lo permite).

### B.5 Paso a paso para el usuario

```bash
# 0) el repo principal, con todo commiteado
# 1) carpeta + rama (git comparte el .git y materializa TODO lo versionado)
git worktree add ../EW8_wt_featureA -b featureA master
cd ../EW8_wt_featureA
# 2) bootstrap de la instancia (genera earthworm.d + db.env)
ew_gui_tools/common/tools/worktree_bootstrap.sh . --instance 1
# 3) compilar (los binarios de los módulos no se versionan)
source ./ew8_unix.sh
make -C ew_gui_tools/common GUI_TOOLKIT=gtk4
for d in wavestore csnloc csnwphase csnmags pickS csntvp csnrv csnstaevdisp ew_controller \
         hypodb hyp2000_ring nlloc_ring csnhypodbp; do make -C ew_gui_tools/$d || break; done
# 4) su base de datos
./ew8_db.sh -i 1 up
# 5) levantar / trabajar / parar
source ./ew8_unix.sh && ./ew_monitor.sh start && ./ew_monitor.sh status
./ew_monitor.sh stop
# 6) terminar
git worktree remove ../EW8_wt_featureA
```

Coste por worktree: ~80 MB de árbol (los dos mapas de 14,7 MB + headers) y **cero** objetos
duplicados en disco (`objects` es compartido); un stack vivo añade su `wstore` y su volumen de BD.

---

## 4. Decisiones de diseño (con su porqué)

- **`params/` es configuración, no runtime**: en un sistema donde los cambios de config se
  versionan y se revisan, un directorio de trabajo dentro de `params/` mezcla estado con fuente de
  verdad. Cada módulo escribe en **su** carpeta de `run_working_v8/`, que el propio módulo crea.
- **Los recursos pesados no se copian ni se symlinkean**: se referencian por ruta absoluta de
  **sólo lectura** en el `.d` (109 GB de `time3d`, SDS, GF de fase W, overlays, perfiles). Un
  *symlink* sería una indirección extra sobre datos que no son configuración.
- **Las claves de escritura son relativas** (`../hyp2000_ring`): así la configuración es **idéntica**
  en todos los worktrees (sin valores por máquina) y el aislamiento sale del propio árbol de
  carpetas. La resolución se apoya en que el cwd de los módulos es `$EW_PARAMS` (V6).
- **`earthworm.d` es lo único generado** (opción G): las keys SysV son un recurso global de la
  máquina; versionar el offset sería llevar topología al repo y arriesgar un merge que rote las
  keys de `master`. El resto del fichero (anillos, módulos, mensajes) sí se versiona en el `.base`.
- **BD por contenedor (opción B)**: es el único TCP que colisiona; el resto de los módulos son
  clientes (el único `bind` era `wave_serverV`, ya eliminado).
