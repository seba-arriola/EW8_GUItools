Guía operativa de worktrees de `EW8_GUItools`: cargar el entorno, trabajar en el principal, y
crear / usar / eliminar un worktree con su base de datos aislada. El procedimiento canónico (y la
validación de aislamiento entre dos stacks) está en [`WORKTREES.md`](WORKTREES.md); esto es la
chuleta de comandos.

Cada paso va marcado: ✅ **necesario** · 🔎 **verificación** · 👍 **recomendado** · 💤 **opcional**.

Los comandos usan `<ruta-del-repo>` en lugar de una ruta absoluta: el invariante
`common/tests/test_no_repo_path.sh` (dentro de `make -C ew_gui_tools/common check`) prohíbe
escribir la ruta absoluta del repo en ficheros versionados, justo para que el repo se pueda mover
o clonar en otra carpeta sin que nada quede apuntando a la vieja.

---

# 1. Cargar el entorno — ✅ NECESARIO en **cada** terminal nueva

```bash
cd <ruta-del-repo>
source ./ew8_unix.sh
```

Define `EW_HOME`(raíz del repo), `EW_VERSION`(`ew_8.0`), `EW_PARAMS`(`run_working_v8/params`), `EW_LOG`, el `PATH` a `ew_8.0/bin` y el shim GNU de `timeout`, y carga `ew8_gtk_env.sh` (GTK en WSLg). Sin esto, ni los binarios ni los módulos encuentran su entorno. Todo se deriva de la ruta del script ⇒ mover el repo no rompe nada (verificado: `EW_PARAMS` apunta a la ruta nueva).

> Nota: los módulos se lanzan a mano **desde `run_working_v8/params`** (los `.d` abren rutas relativas). Para el día a día **no** hace falta: lo hace `ew_monitor.sh`.

---

# 2. ¿La compilación es siempre necesaria?

| situación | ¿compilar? |
|---|---|
| Repo principal `EW8_GUItools` (hoy) | ❌ **No**: `check_install.sh -v` da **16/16 OK** |
| **Worktree nuevo** | ✅ **Sí, siempre y una vez**: los binarios de nuestros módulos **no se versionan** (`.gitignore` ignora `/ew_8.0/bin/csnloc`, `csnhypodbp`, …, 16 en total). Solo se versionan **8 de fábrica** (`startstop`, `status`, `slink2ew`, `pick_FP`, `sniffring`, `putpick`, `restart`, `stopmodule`) |
| Tras tocar un módulo | ✅ solo ese módulo: `make -C ew_gui_tools/<mod>` |
| Tras tocar `common/` | ✅ `common` **primero** (si no: `undefined reference`, o peor, ABI en silencio) |
| Solo para arrancar la BD / el monitor | ❌ no, pero `startstop` dejaría el módulo en `NoExec` → compilá antes de `start` |

**Bloque de compilación (el de `docs/WORKTREES.md` §1 paso 3):**

```bash
cd <ruta-del-repo>            # ¡la raíz de ESTE repo/worktree, no la del principal!
source ./ew8_unix.sh
echo "$EW_HOME"               # 🔎 debe imprimir la raíz de ESTE repo/worktree
make -C ew_gui_tools/common                          # la lib compartida, primero
for d in wavestore csnloc csnwphase csnmags pickS csntvp csnrv csnstaevdisp \
         ew_controller hypodb hyp2000_ring nlloc_ring csnhypodbp; do
  make -C ew_gui_tools/$d || break
done
sh ew_gui_tools/common/tools/check_install.sh -v        # QUERÉS 16/16 "OK"
```

Tres detalles que importan:
- **`make` a secas ya instala** en `ew_8.0/bin`: el target por defecto es `all: <target> install` (verificado en `csnloc`, `csnhypodbp`, `csnmags`); no hace falta `make install`.
- ⚠️ **`source` va ANTES del `make`, y sobre ESTE repo**: el destino de instalación lo pone el Makefile (`install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin`). Sin entorno no compila (las cabeceras y librerías quedan en `//include`/`//lib` y el `install` aborta con `no existe el directorio destino //bin`); y si el entorno venía cargado **desde otro repo** (p. ej. dejaste el `source` del principal en esa terminal) compila igual pero **instala los binarios en el otro repo**. De ahí el `echo "$EW_HOME"`.
- ⚠️ **`check_install.sh` no falla si no compilaste**: imprime `SKIP <binario> (módulo … sin compilar)` y **sale 0**. No te fíes del exit code: mirá que **no quede ningún `SKIP`**. `FALTA`/`DESACTUALIZADO` sí dan exit 1 con el comando exacto a correr.

🔎 ¿Hace falta `make clean`? Solo si ya compilaste y cambiaste `common/` (la regla de la lib no depende de las fuentes):

```bash
make -C ew_gui_tools/common clean && make -C ew_gui_tools/common
```

---

# 3. Escenario A — trabajar en el principal (instancia 0)

```bash
source ./ew8_unix.sh              # ✅
./ew8_db.sh status                # 🔎 ¿sana y con tablas=6? (exit 0 ok · 2 no sana)
./ew8_db.sh up                    # ✅ si estaba abajo (idempotente; espera healthcheck)
db/test_schema.sh -i 0            # 👍 verifica las 6 tablas por nombre (AC1)
./ew_monitor.sh start             # ✅ stack completo en background (setsid)
./ew_monitor.sh status            # 🔎 RUNNING + pid + una fila por módulo con su estado
sh ew_gui_tools/common/tools/diag.sh process   # 🔎 qué módulos del .d están corriendo
./ew_monitor.sh stop              # ✅ parada ordenada (SIGTERM a startstop e hijos)
```

> `status` **sale 1 cuando está parado** (no es un error). Y `./ew_monitor.sh restart csnmags` solo si el nombre no está repetido: hay **dos** `csnmags` (`csnmags.d` y `csnmags_raw.d`) → con nombre ambiguo avisa y **no manda nada**; ahí se pasa el **pid**.

💤 GUI opcional para manejar módulos y editar `.d`: `ew_controller ew_controller.d`.

---

# 4. Escenario B — pipeline completo de un worktree nuevo

Regla de oro: **un `N` por worktree**, `N ≥ 1`; `N = 0` está reservado al principal.

| instancia | contenedor | puerto | keys de anillo | volumen |
|---|---|---|---|---|
| 0 (principal) | `ew8_hypodb` | 5435 | base | `ew8_hypodb_ew8_db_data` |
| 1 | `ew8_hypodb_1` | 5436 | +10000 | `ew8_hypodb_1_ew8_db_data` |
| 2 | `ew8_hypodb_2` | 5437 | +20000 | `ew8_hypodb_2_ew8_db_data` |
| N | `ew8_hypodb_N` | 5435+N | +10000·N | `ew8_hypodb_N_ew8_db_data` |

**Paso 1 — ✅ crear el worktree** (desde el principal; `master` es el commit de partida, podés poner cualquier rama/commit):

```bash
cd <ruta-del-repo>
git status                        # 🔎 limpio (sin cambios sin commitear)
git worktree add ../wt_featureA -b featureA master
cd ../wt_featureA
```

**Paso 2 — ✅ bootstrapear la instancia** (genera `earthworm.d` con keys desplazadas y crea `db.env` con `DB_PORT=5436` copiando tus credenciales del principal):

```bash
./ew_gui_tools/common/tools/worktree_bootstrap.sh . --instance 1
```

💤 `--print-plan` enseña lo que haría sin escribir nada.

**Paso 3 — 🔎 verificar sin escribir** (👍 muy recomendado, sobre todo antes de arrancar):

```bash
./ew_gui_tools/common/tools/worktree_bootstrap.sh . --check
```

Comprueba: `earthworm.d` = base+offset (drift), el `N` deducido, `db.env` con el puerto correcto, **keys disjuntas con los otros worktrees**, puerto libre y de quién es el contenedor, y si hay un stack vivo en otra carpeta. **No escribe nada** (salvo un `/tmp`). Sale **0** OK · **2** uso · **3** entorno · **4** problemas.
💤 `--refresh` regenera (idempotente) y reporta drift; `--force` permite `--instance 0` dentro de un worktree (colisiona con el principal: solo si no corren a la vez).

**Paso 4 — ✅ compilar** (en un worktree nuevo es obligatorio; el bootstrap **no** compila ni avisa de binarios ausentes). El `source` va **aquí**, antes del `make`, y sobre la raíz de ESTE worktree:

```bash
cd ../wt_featureA                 # la raíz del worktree, no la del principal
source ./ew8_unix.sh              # ⚠️ ANTES de compilar: fija EW_HOME
echo "$EW_HOME"                   # 🔎 debe ser la raíz de ESTE worktree
# … y sigue el bloque de §2 (common + los 13 módulos + check_install.sh -v)
```

**Paso 5 — ✅ su base de datos aislada** (el entorno ya quedó cargado en el paso 4):

```bash
./ew8_db.sh -i 1 up               # contenedor ew8_hypodb_1, puerto 5436, volumen propio
./ew8_db.sh -i 1 status           # 👍 5436 + tablas=6 (el esquema entra solo al crear el volumen)
db/test_schema.sh -i 1            # 💤 lo mismo, pero exigiendo las 6 tablas por nombre
```

**Paso 6 — ✅ arrancar su stack**:

```bash
./ew_monitor.sh start
./ew_monitor.sh status            # pid PROPIO de startstop + 15 módulos Alive
sh ew_gui_tools/common/tools/diag.sh process
```

**Paso 7 — 🔎 validar el aislamiento si tenés los dos stacks vivos a la vez** (opcional pero es la prueba de fuego):

```bash
cd <ruta-del-repo>
sh ew_gui_tools/common/tools/worktree_two_stacks_check.sh ../wt_featureA
sh ew_gui_tools/common/tools/worktree_two_stacks_check.sh ../wt_featureA --sniff   # 💤 muestrea PICK_RING
```

Comprueba CA-1 keys disjuntas · CA-2 dos `startstop` distintos, `Alive`, sin pids compartidos · CA-4 `diag process` · CA-5 puerto→contenedor esperado · CA-6 segmentos SysV vivos sin cruces · CA-10 cwd de los refinadores dentro de su worktree. Sale 4 si alguna falla. Necesita **ambos stacks arriba**.

**Paso 8 — trabajar.** ¿Cerraste la terminal? En la del worktree: `cd ../wt_featureA && source ./ew8_unix.sh`.

---

# 5. Eliminar un worktree **con su base** (el orden importa)

```bash
cd ../wt_featureA
./ew_monitor.sh stop                 # 1) ✅ parar SU stack
./ew8_db.sh -i 1 down                # 2) 💤 detener la BD (conserva el volumen y los datos)
./ew8_db.sh -i 1 destroy --yes       # 3) ✅ BORRAR de verdad: contenedor + volumen + red, sin recrear

cd <ruta-del-repo>                   # 4) recién ahora, el worktree
git worktree remove ../wt_featureA
git branch -d featureA               # 💤 si ya no querés la rama
```

**¿Ya borraste el worktree y quedó la base huérfana?** Desde el principal, sin `db.env` de por medio:

```bash
./ew8_db.sh instances                # N / contenedor / estado / puerto / volumen / WORKTREE
#   ... 1  ew8_hypodb_1  Exited ...  5436  ew8_hypodb_1_ew8_db_data  huerfana (sin worktree registrado)
./ew8_db.sh -i 1 destroy --yes
```

Detalles:
- **`down` ≠ `destroy`**: `down` quita contenedor+red y **conserva el volumen**; `destroy` (`down -v`) borra contenedor+**volumen**+red y **no recrea**; `reset` borra y **vuelve a levantar**.
- `destroy` de la **instancia 0** exige `--yes` explícito; sin `--yes` y sin terminal **aborta sin tocar docker**.
- `destroy`/`instances` **no** exigen que el `db.env` local declare ese `DB_PORT` (solo avisan) ⇒ podés limpiar instancias ajenas.
- Si borraste el directorio a lo bruto: `git worktree prune` limpia los metadatos.
- `git worktree remove` se niega si hay **cambios sin commitear** (los artefactos ignorados: binarios, logs, `earthworm.d`, `db.env` **no** cuentan). Si te lo pide, mirá qué son antes de `--force`.
- ⚠️ **Nunca muevas el repo con worktrees registrados** (rutas absolutas dentro de `.git`). Si pasa: `git worktree repair`.

---

# 6. Chuleta de herramientas (todos los flags)

### `ew_gui_tools/common/tools/worktree_bootstrap.sh [<carpeta>] [flags]`
`--instance N` (N≥1 en worktrees; default: el del generado, si no 0) · `--check` (no escribe; exit 4 con problemas) · `--refresh` · `--print-plan` · `--force` · `-h`.
Escribe **solo**: `run_working_v8/params/earthworm.d` (generado, offset) y `db.env` **si falta** (respeta el tuyo). **No** crea directorios de runtime (los crea cada módulo) y **no** compila.
Exit: `0` OK · `2` uso · `3` entorno · `4` `--check` con problemas.

### `./ew8_db.sh [-i N] <subcomando>` — exit `0` ok · `1` uso/docker ausente · `2` base no sana
| subcomando | qué hace | ¿exige `DB_PORT=5435+N`? |
|---|---|---|
| `up` | `compose up -d --wait` + estado (crea esquema al crear el volumen) | ✅ sí |
| `status` | contenedor + healthcheck + `tablas=N` | ❌ avisa |
| `down` | detiene, **conserva** el volumen | ❌ avisa |
| `reset` | confirma, `down -v` y **reaplica** esquema | ✅ sí |
| `destroy [--yes]` | **borra** contenedor+volumen+red sin recrear | ❌ avisa |
| `instances` | tabla de instancias (marca huérfanas); no lleva args | ❌ avisa |
| `logs` / `psql [args]` | logs en vivo / consola psql | ❌ / ✅ |
| `deps` / `-h` | comprueba docker+compose / ayuda | — |
Admite `-i N`, `--instance N`, `--instance=N`, `-iN`, `EWGUI_INSTANCE=N`.

### `./ew_monitor.sh <status|list|start|stop|restart> [<módulo>…] [--dry-run] [--force]`
`--dry-run`/`--force` solo con acción por módulo. `<módulo>` = binario, tallo del `.d` o **pid** (ambiguo ⇒ avisa y no envía nada). `status` sale **1** si está parado. Env: `EW_MONITOR_CFG`, `EWMON_VERIFY_TIMEOUT=15`, `EWMON_PIDFILE`, `EWMON_PID_SCOPE=strict|loose`, `STARTSTOP_CONSOLE`, `STOP_TIMEOUT=30`.

### `sh ew_gui_tools/common/tools/diag.sh <sub>` — exit `0` ok · `1` fallo · `2` uso · `3` prohibido · `4` ya corre
**Nivel A (no arranca nada):** `env` · `install` · `config <csnhypodbp|csnrv|csnwphase>` · `profiles` · `overlays <csnrv|csnstaevdisp>` · `rings [anillo…]` · `log <módulo> [n]` · `logs` · `errors [n]` · `process` · `help`
**Nivel B (arranca UNO, acotado):** `run <módulo> [seg] [--bg] [--force]` · `attached <módulo>` · `stop <módulo>`
Nunca hace `startstop`/`restart`/`make`/`git`: para eso está `ew_monitor.sh`.

### `sh ew_gui_tools/common/tools/check_install.sh [-v]` — único flag `-v` (lista `OK`/`SKIP`/`FALTA`/`DESACTUALIZADO`); exit `0` al día · `1` falta/desactualizado · `2` sin destino.

### `db/test_schema.sh [-i N]` — exit `0` 6 tablas · `1` no arranca o falta una tabla · `2` uso o falta `db.env`
Levanta la base de **esa** instancia (delega en `./ew8_db.sh -i N up`) y exige las 6 tablas por nombre. Sin `-i` es la instancia 0. **No tiene compose propio**: tenerlo cruzaba instancias (contenedor de la 0 con el puerto y el volumen del worktree → `Bind for 0.0.0.0:5436 failed`). Lo vigilan `common/tests/test_db_scripts_instance.sh` (fuente + doc) y `test_db_test_schema_instance.sh` (comportamiento).

---

# 7. Diagnóstico rápido (cuando algo no cuadre)

```bash
./ew_monitor.sh status                       # ¿RUNNING? ¿algún módulo Dead?
sh ew_gui_tools/common/tools/diag.sh process  # ¿qué corre?
sh ew_gui_tools/common/tools/diag.sh logs     # inventario de logs
sh ew_gui_tools/common/tools/diag.sh errors   # errores de los logs recientes
sh ew_gui_tools/common/tools/diag.sh log csnloc 40
tail -f run_working_v8/log/startstop_unix_*.log
./ew8_db.sh status                           # ¿tablas=6?
./ew8_db.sh instances                        # ¿bases huérfanas de worktrees ya borrados?
```

Síntoma típico: módulo que no arranca y en la consola de `startstop` aparece `execvp failed`/`NoExec` ⇒ **falta compilar/instalar** ese binario (`check_install.sh -v`, mirando los `SKIP`).

---

**Resumen de lo que es imprescindible:** `cd` a la raíz del repo/worktree + `source ./ew8_unix.sh` (**también en un worktree, y antes de compilar**: fija `EW_HOME`) → `git worktree add` → `worktree_bootstrap.sh . --instance N` → **compilar** → `./ew8_db.sh -i N up` → `./ew_monitor.sh start`. Todo lo demás (`--check`, `test_schema.sh -i N`, `check_install.sh -v`, `two_stacks_check.sh`, `diag.sh`) es verificación, y conviene `--check` + `check_install.sh -v` en cada worktree nuevo: son los dos que detectan el 90 % de los problemas antes de arrancar.
