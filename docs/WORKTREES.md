# WORKTREES — trabajar en paralelo con varios stacks EarthWorm

Cada **worktree** de git es una carpeta con su propio árbol de trabajo, su rama, sus **anillos**,
su **base de datos** y sus **logs**. No comparten nada escribible: se pueden tener dos (o más)
stacks vivos a la vez.

- El repo principal (`master`) es la **instancia 0** (keys base, `DB_PORT=5435`).
- Cada worktree nuevo es una **instancia N ≥ 1**: keys `+10000·N`, `DB_PORT=5435+N`, contenedor y
  volumen de BD propios.

---

## 1. Crear un worktree (paso a paso)

```bash
# 0) El repo principal, con todo commiteado (un worktree materializa un COMMIT).
cd <ruta-del-repo>
git status                      # limpio

# 1) Carpeta + rama (git comparte .git; el árbol se materializa completo)
git worktree add ../EW8_wt_featureA -b featureA master
cd ../EW8_wt_featureA

# 2) Bootstrap de la instancia: genera params/earthworm.d (keys +10000·N)
#    y params/db.env (DB_PORT=5435+N).
ew_gui_tools/common/tools/worktree_bootstrap.sh . --instance 1
ew_gui_tools/common/tools/worktree_bootstrap.sh . --check   # tiene que dar OK

# 3) Compilar (los binarios de los módulos NO se versionan: son de cada worktree)
source ./ew8_unix.sh
make -C ew_gui_tools/common
for d in wavestore csnloc csnwphase csnmags pickS csntvp csnrv csnstaevdisp ew_controller \
         csnrvQT csnstaevdispQT ew_controllerQT csntvpQT \
         hypodb hyp2000_ring nlloc_ring csnhypodbp; do make -C ew_gui_tools/$d || break; done
sh ew_gui_tools/common/tools/check_install.sh -v   # 16/16 OK

# 4) Su base de datos (contenedor/volumen/puerto propios, sin auto-arranque)
./ew8_db.sh -i 1 up
db/test_schema.sh -i 1                 # verifica las 6 tablas de ESTA instancia

# 5) Trabajar (el entorno ya se cargo en el paso 3; si abriste una terminal NUEVA
#    —o la tuya tiene EW_PARAMS de OTRO repo— vuelve a hacer `source ./ew8_unix.sh`)
./ew_monitor.sh start
./ew_monitor.sh status          # sólo los módulos de ESTA instancia
#  … o un módulo suelto:  cd run_working_v8/params && csnhypodbp csnhypodbp.d

# 6) Parar
./ew_monitor.sh stop

# 7) La base se resuelve ANTES de borrar el worktree: después ya no hay un
#    db.env de esa instancia desde donde correr ew8_db.sh.
./ew8_db.sh -i 1 down            # la DETIENE (conserva el volumen y los datos)
./ew8_db.sh -i 1 destroy --yes   # la BORRA de verdad (contenedor+volumen+red)
#
#    ¿Ya borraste el worktree y quedó huérfana? Desde el principal:
#      ./ew8_db.sh instances            # qué N quedó y si está huérfana
#      ./ew8_db.sh -i 1 destroy --yes

# 8) Recién entonces, el worktree
cd <ruta-del-repo>
git worktree remove ../EW8_wt_featureA
```

**Un worktree nuevo no necesita nada más**: hereda de git `ew_8.0/` (la instalación EarthWorm
vendorizada), `ew_gui_tools/`, toda la configuración (`run_working_v8/params/`), `db/` y `docs/`.
Lo que **no** hereda (es runtime, ignorado) es: `ew_8.0/bin/<módulos>` (los pone `make install`),
`run_working_v8/params/db.env`, `log/`, `wstore/` y las carpetas de trabajo de los módulos.

## 2. Qué es "instancia" y dónde vive

| Dato | Dónde | Por qué |
|---|---|---|
| **Offset de las keys de anillo** | `run_working_v8/params/earthworm.d` **generado** (ignorado) a partir de `earthworm.d.base` (versionado, offset 0) | Las keys SysV son globales de la máquina: son topología, no configuración. `worktree_bootstrap.sh --instance N` aplica `+10000·N` |
| **Puerto/BD** | `run_working_v8/params/db.env` (ignorado): `DB_PORT=5435+N` | Es el único TCP que colisiona; el resto de los módulos son clientes |
| Work dirs, logs, store | `run_working_v8/{hyp2000_ring,nlloc_ring,csnhypodbp}/`, `log/`, `wstore/` **dentro de cada worktree** | Salen gratis: cada worktree es otra carpeta. Los crea cada módulo (`mkdir -p`) |
| Recursos pesados (grillas NLLoc, SDS, GF de fase W, overlays, perfiles) | **fuera del repo**, de sólo lectura, referenciados por ruta ABSOLUTA en los `.d` | No se copian ni se symlinkean por worktree |

`N` se deduce del propio `earthworm.d` generado (`offset/10000`) y se cruza con `db.env`: si no
cuadran, `--check` lo dice. No hay ningún fichero marcador.

## 3. Qué se aísla (y qué no)

| Frontera | Aislamiento |
|---|---|
| Anillos SysV | **keys distintas** (`earthworm.d` generado por instancia) |
| BD PostgreSQL | contenedor/volumen/proyecto propios y puerto `5435+N` (`./ew8_db.sh -i N`) |
| Ficheros que escribe cada módulo | su carpeta en `run_working_v8/`; **`params/` es sólo configuración versionada** |
| Logs y estado | `run_working_v8/log/` (incluye `csnloc.events`) |
| Store de ondas | `run_working_v8/wstore/` (`StoreRoot ../wstore`) |
| `ew_monitor.sh` / `diag.sh` | identidad por **pid** (`$EW_LOG/startstop.pid` + `/proc/<pid>/environ` filtrado por `EW_PARAMS`): `status`/`stop` de uno no tocan el otro |
| GUIs | los 5 módulos usan `G_APPLICATION_NON_UNIQUE`: dos instancias conviven |
| `slink2ew` | cada stack abre su propia conexión al servidor SeedLink |
| Recursos externos | `/mnt/d/resources/**` y `/mnt/sds/**` se **leen** desde todos los worktrees |

Las **dos únicas** escrituras inevitables en `params/` son el estado interno de binarios de
fábrica, que no tienen clave de ruta en su `.d`: `slink<modid>.state` (`slink2ew`) y
`pick_FP_<modid>.ndx` (`pick_FP`) — ignorados por patrón. Invariante:
`common/tests/test_params_stays_clean.sh`.

## 4. Criterios de aceptación (dos stacks vivos a la vez)

Con **A** = repo principal (instancia 0) y **B** = worktree (instancia 1), los dos arriba:

| CA | Comprobación |
|---|---|
| CA-1 | `ipcs -m` muestra dos juegos de keys **disjuntos** (0–2000 vs 10000–12000) |
| CA-2 | `./ew_monitor.sh status` en B lista **sólo** sus 15 módulos, con sus pids |
| CA-3 | `./ew_monitor.sh stop` en B deja los 15 de A `Alive` (y al revés) |
| CA-4 | `diag.sh process` en cada uno lista sólo sus pids (sin falsos `YA CORRE`) |
| CA-5 | `docker ps`: `ew8_hypodb` (5435) y `ew8_hypodb_1` (5436); insertar en una BD no cambia la otra |
| CA-6 | **estructural**: `ipcs -m` muestra **los dos** segmentos de cada anillo (32 = 2×16) además de las keys disjuntas de CA-1 ⇒ un mensaje de A no puede caer en el ring de B. Con `--sniff`, además se muestrea el tráfico de cada `PICK_RING` |
| CA-7 | `find /mnt/d/resources/nlloc /mnt/d/resources/hyp2000 -newer <centinela>` → vacío |
| CA-8 | `run_working_v8/params` no gana ficheros (salvo las 2 salvedades de fábrica) |
| CA-9 | Los dos `slink2ew` siguen `Alive` (el servidor SeedLink acepta 2 clientes) |
| CA-10 | `ls /proc/<pid>/cwd`: los refinadores de cada stack, en la carpeta de **su** worktree |

## 6. Validación (evidencia, 2026-10-15)

`ew_gui_tools/common/tools/worktree_two_stacks_check.sh <worktree> [--sniff]` comprueba las CA
contra los dos stacks **vivos** (es herramienta, no test de `check`: necesita dos stacks). Corrida
real con A = repo principal (instancia 0) y B = worktree (instancia 1):

| CA | Resultado |
|---|---|
| CA-1 | keys disjuntas: A 16 claves (0–2000) / B 16 (10000–12000) |
| CA-2 | `startstop` 596491 (A) y 596595 (B); 15+15 `Alive`; **0 pids compartidos** entre los dos `status` |
| CA-3 | `stop` en B → A sigue `RUNNING` con sus 15 módulos (y el 2.º `stop` los para) |
| CA-4 | `diag process`: 15 procesos en cada uno |
| CA-5 | `ew8_hypodb` (5435) + `ew8_hypodb_1` (5436), 6 tablas cada uno |
| CA-6 | 10 segmentos SysV vivos por instancia, **ninguno compartido** (20 keys vivas en total) |
| CA-7 | nada escrito en `/mnt/d/resources` desde el centinela |
| CA-8 | `params` de cada instancia sólo gana `slink*.state` (salvedad de binario de fábrica) |
| CA-10 | los refinadores de cada stack trabajan con su cwd dentro de **su** worktree |

Además, un worktree nuevo **se compila entero con lo versionado** (`common` + 13 módulos `rc=0`,
`check_install` 16/16) sin tocar el principal, y el aislamiento sin dos stacks está cubierto por
`common check`: `test_ew_monitor_scope.sh` (CA-3 con dos `startstop` reales) y
`test_params_stays_clean.sh` (params limpio).

```bash
ew_gui_tools/common/tools/worktree_bootstrap.sh [<carpeta>] [--instance N]
        [--check] [--refresh] [--print-plan] [--force]
#   exit 0 OK · 2 uso · 3 entorno · 4 --check con problemas
#   --check no escribe: drift del earthworm.d generado, N, db.env, keys disjuntas
#   entre worktrees, puerto (y de quién es el contenedor) y stacks vivos en otras carpetas.
#   --refresh: regenera (idempotente) y reporta drift.  --force: permite --instance 0
#   en un worktree (colisiona con el principal: solo si no corren a la vez).
```

- `./ew8_db.sh instances` y `./ew8_db.sh -i N destroy [--yes]` (desde el repo principal) listan y
  borran la base de una instancia **aunque su worktree ya no exista**.
- Tras editar `earthworm.d.base` hay que regenerar: `worktree_bootstrap.sh . --refresh`.
- Los tests de `common check` usan pids sintéticos y por eso corren con
  `EWMON_PID_SCOPE=loose` / `DIAG_PID_SCOPE=loose`; el invariante real del aislamiento es
  `common/tests/test_ew_monitor_scope.sh` (dos `startstop` reales con `EW_PARAMS` distintos).
- Coste por worktree: ~80 MB de árbol (los dos mapas de 14,7 MB + headers) y **cero** objetos
  duplicados en disco; un stack vivo añade su `wstore` y su volumen de BD.
