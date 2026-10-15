# PLAN — Worktrees de git para paralelizar el trabajo en EW8_GUItools

**Estado: preparación HECHA; falta el bootstrap.** Este documento refleja el repo tras la
limpieza de 2026-10-15. Lo implementado está marcado ✅; lo que queda pendiente, ⏳.

---

## 1. Lo que ya está hecho (preparación del repo)

✅ **`earthworm_8.0/` (1004 MB) → `ew_8.0/` (8,2 MB), versionado.**

`ew_8.0/` es un **subconjunto prebuilt** de la instalación EarthWorm 8, ahora **trackeado**:
`include/`, `lib/{libew_mt.a,libmseed2.a,rw_mag.o}` y `bin/{startstop,status,restart,stopmodule,
slink2ew,pick_FP,sniffring,putpick}`. Ver `ew_8.0/VENDORED.md` (versión, fecha de build, licencia y
cómo se regenera). **Consecuencia clave: un worktree nuevo ya trae EarthWorm** — el *symlink-farm*
que este plan describía antes **ya no hace falta**.

- `EW_VERSION` pasó a `ew_8.0` (`ew8_unix.sh:13`, `common/Makefile:14`, `check_install.sh:31`, los
  `EWGUI_BIN` de los tests y el fallback de `hypodb`).
- `src/` ya no se usa: `csnstaevdisp` enlaza `lib/libew_mt.a` (traía los mismos 8 objetos).
- `.gitignore` §3b: los binarios de **fábrica** de `ew_8.0/bin` se versionan; los que instala
  `make install` (nuestros módulos) se ignoran.

✅ **Limpieza de lo que se fue a Legacy.** Fuera del repo: `tank_tools/`, `mseed/`, `picks/*`,
`picks_ps/`, `picks_s/`, `replay/`, `run_working_v8/tanks/`, `api_report_locs/` y los `.d`/backups
(`startstop_replay.d`, `tankplayer.d.tmpl`, `wave_serverV.d`, `decimate_*.d`, `scnl2scn.*`,
`slink2ew_BHZ.d`, `csnloc_legacy.d`, `csnhypodbp.c_old_bkp`, `Makefile_old_bkp`,
`tests_soluciones_publicadas.dat`). Se purgaron sus rastros en `AGENTS.md`, `README.md`, `docs/`,
los READMEs de módulo, `diag.sh` y los comentarios de los `.d`.

✅ **`wave_serverV` eliminado de verdad** (no solo los rastros): se quitó el backend TCP
(`WSRC_WS`/`WSRC_AUTO`, `ws_get`, `wsMenu`, `WsIP`/`WsPort`/`WsTimeout`/`WaveBackend`,
`SocketSysInit`) de `csnmags`, `wavestore` y `csnhypodbp`, junto con los objetos `ws_clientII.o`/
`socket_ew*.o` de los 6 Makefiles que los enlazaban. El hilo de `csnhypodbp_ws.c` **se conserva**
(lo comparten store y SDS). Fuentes de ondas: **store + SDS**.

✅ **`params/`:** se trackean `iasp91.tbl`/`iasp91.hed` (sin ellos `csnloc` arrancaba **en
silencio** sin modelo de tiempos) y se borraron los huérfanos (`ew_linux.bash`, `response/`,
`startstop.desc`, `slink2ew.desc`).

✅ **Base de datos por instancia (opción B).** `db/docker-compose.yml` interpola
`${COMPOSE_PROJECT_NAME}`/`${DB_PORT}`/`${DB_RESTART}` y `ew8_db.sh` acepta `-i N`:
`./ew8_db.sh -i 2 up` levanta `ew8_hypodb_2` en `5437` con contenedor, red y volumen propios y
`restart: no`. Verifica que el `db.env` de ese worktree declare el mismo `DB_PORT`.

## 2. Estado actual del repo

| Categoría | Estado |
|---|---|
| `ew_gui_tools/` (14 módulos), `db/`, `docs/`, `ew_8.0/` | tracked ✅ |
| `run_working_v8/params/` (config + IASP91 + grillas + mapas) | tracked ✅ |
| `run_working_v8/params/db.env` | ignorado (se copia de `.example`) |
| `run_working_v8/{log,wstore}` | ignorados (runtime) |
| Todo lo movido a Legacy | no existe en el repo ✅ |
| Binarios de módulos en `ew_8.0/bin` | ignorados (los pone `make install`) |

Un worktree nuevo hereda **todo** lo necesario salvo dos cosas de runtime (abajo).

## 3. Lo que queda por hacer

### 3.1 Prerequisito

⏳ Commitear el estado actual (los cambios están sin commitear; un worktree sale de un commit).

### 3.2 `worktree_bootstrap.sh` (contrato, simplificado)

```
ew_gui_tools/common/tools/worktree_bootstrap.sh <worktree-path>
    [--instance N] [--refresh] [--print-plan] [--check]
```

- Valida que `<wt>` es un worktree de este repo (`git rev-parse --show-toplevel`, `.git` de fichero).
- Copia `run_working_v8/params/db.env` (si falta) desde el repo principal.
- Crea `run_working_v8/log/` (lo necesita `startstop`/`logit`).
- Escribe `<wt>/.ewgui-instance` (`{instance, offset}`).
- Con `--instance N`: crea `run_working_v8/params_inst_<N>/` (copia gitignored de `params/`, ~76 MB)
  y aplica los overrides de instancia.
- Idempotente; `--check` verifica el layout y que las keys no solapen.
- **No escribe nunca fuera de `<wt>`** y **no toca la BD** (para eso está `ew8_db.sh -i N`).

### 3.3 Anillos por instancia (lo único que colisiona hoy)

`GetKey()` resuelve nombre→key leyendo `$EW_PARAMS/earthworm.d`, y **ningún módulo hardcodea keys**,
así que basta un mapa propio por instancia: `offset = 10000·N` sobre **todas** las keys (incluidas
`FLAG_RING` y `NAMED_EVENT_RING`). `ew8_unix.sh` debe resolver `EW_PARAMS` así:
`EWGUI_PARAMS_DIR` → si existe `<DIR>/.ewgui-instance` entonces `params_inst_<N>` → si no
`run_working_v8/params`. Los puertos **no** colisionan (todos los módulos son clientes; el único
`bind` era `wave_serverV`, ya eliminado).

### 3.4 Herramientas por worktree

- `ew_monitor.sh`: pidfile `$EW_LOG/startstop.pid` (con fallback a `pgrep -x` validado), porque
  `get_startstop_pid` toma **el primer `startstop` del sistema** y `stop` mata por nombre en todo el
  sistema.
- `diag.sh`: `TMP` por worktree (`$ROOT/run_working_v8/.diag`) y `process`/`run` acotados a su
  `EW_PARAMS` (hoy un worktree da falsos `YA CORRE` por los procesos del otro).

## 4. Criterios de aceptación pendientes

- **CA1** `diag.sh env` en el worktree muestra `EW_PARAMS`/`EW_LOG`/`bin` dentro del worktree.
- **CA2** `make -C ew_gui_tools/common check` pasa en el worktree (ya no depende de nada externo).
- **CA3** `make` de un módulo instala en `<wt>/ew_8.0/bin/<mod>` sin tocar el binario del principal.
- **CA4** bootstrap idempotente; `--check` detecta solapes de keys.
- **CA5** con dos stacks arriba, `ew_monitor.sh status` de cada worktree lista **solo sus** módulos.
- **CA6** `docker compose config` con `COMPOSE_PROJECT_NAME` distinto → nombre/puerto/volumen
  distintos. ✅ (ya verificado con `-i 2`).

## 5. Notas y costes

- `docker compose -f db/docker-compose.yml config` (instancia 0) sigue dando `ew8_hypodb` en 5435:
  **comportamiento idéntico al de siempre**.
- Cada worktree con stack vivo escribe su propio `run_working_v8/wstore` (disco).
- `ew_8.0/lib` conserva `libew_mt.a`, `libmseed2.a` y `rw_mag.o` (`ws_clientII.o`/`socket_ew*.o` ya
  se borraron: nadie los enlaza).
- Se conservan **a propósito** (legítimos): `libmseed2` (librería, no carpeta `mseed/`), el aviso
  "PROHIBIDO LEER `mseed`" ya eliminado, y el tank **offline** de `csnloc`/`pickS`/`csnmags`.
