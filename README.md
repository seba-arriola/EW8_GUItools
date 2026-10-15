# EW8_GUItools: sistema portátil de procesamiento sísmico EarthWorm 8

**EW8_GUItools** es un entorno portátil de adquisición, procesamiento en tiempo real y análisis de
datos sísmicos, construido sobre **EarthWorm 8.0** y ampliado con interfaces gráficas (GUI) propias
y módulos de procesamiento sismológico (`ew_gui_tools`).

La asociación de fases y la primera localización las hace el módulo nativo **`csnloc`**
**localmente** (ver `ew_gui_tools/csnloc/README.md`). Los eventos resultantes alimentan el cálculo
de magnitud y su visualización. Las formas de onda las sirve un **store indexado** propio
(`wstore`), poblado por `wstore_ingest` desde `SLINK_RING`.

---

## Arquitectura

```
SeedLink (10.54.217.9:18000)
        │  slink2ew
        ▼
   SLINK_RING ──────────────► wstore_ingest ──► store indexado (wstore)
        │  pick_FP                                   ondas: store → SDS
        ▼
   PICK_RING ──────────────► csntvp / csnhypodbp (re-picks manuales)
        │  csnloc (asociador + localizador local)
        ▼
   HYPO_RING (TYPE_HYP2000ARC) ──► csnmags  (ML / Mwp / Mb / Ms)  [store/SDS]
                               ──► csnhypodbp  (GUI)              [store/SDS]
                               ──► csnrv       (vista regional)
```

> `earthworm_picks` (en `ew2openapi.d`) pertenece al módulo obsoleto `ew2openapi` y **no** se usa.

---

## 📂 Estructura del proyecto

```text
EW8_GUItools/
├── ew_8.0/                 # Instalación base de EarthWorm 8 (vendorizada)
├── ew_gui_tools/           # Módulos y herramientas GUI propias
│   ├── csnmags/            # Cálculo de magnitud (ML/Mwp/Mb/Ms)
│   ├── wavestore/          # Store indexado de ondas (libwavestore.a + wstore_ingest)
│   ├── csnhypodbp/         # Visor de hipocentros / re-picker
│   ├── csnrv/              # Visor regional
│   ├── csntvp/             # Visor de trazas / picker manual
│   ├── csnstaevdisp/       # Visor de estaciones y eventos
│   ├── csnloc/             # Asociación de fases + localización
│   ├── ew_controller/      # Panel de control de startstop
│   ├── _legacy_atwc/       # Herramientas WC/ATWC archivadas (referencia histórica)
│   └── libsrc/             # Legacy WC/ATWC (sin consumidores activos)
├── run_working_v8/         # Runtime de EarthWorm (params, log, wstore)
├── ew8_unix.sh             # Inicialización del entorno
├── ew8_gtk_env.sh          # Entorno GTK4/WSLg (fuente única; la carga ew8_unix.sh)
└── ew_monitor.sh           # Control del servicio EarthWorm
```

---

## Requisitos

- Linux x86_64 (probado en WSL2/WSLg).
- `gcc`, `make`, `pkg-config`, librerías de desarrollo GTK3.
- Acceso al servidor SeedLink `10.54.217.9:18000` (sin él no fluyen formas de onda ni picks; el
  resto de la cadena sigue funcionando).

---

## ⚙️ Preparación del entorno

Carga siempre el entorno antes de compilar o ejecutar:

```bash
source ./ew8_unix.sh
```

Configura EarthWorm (`EW_HOME`, `EW_VERSION`, `EW_PARAMS`, `EW_LOG`, `PATH`).
Consulta `AGENTS.md` para la tabla completa de variables.

---

## 🛠️ Compilación

Recompila todos los módulos propios (cada uno instala su binario en `ew_8.0/bin/`):

```bash
source ./ew8_unix.sh
for dir in ew_gui_tools/csnmags ew_gui_tools/csnhypodbp ew_gui_tools/csnrv \
           ew_gui_tools/csntvp ew_gui_tools/csnstaevdisp ew_gui_tools/ew_controller \
           ew_gui_tools/csnloc; do
  [ -d "$dir" ] && (cd "$dir" && make clean && make)
done
```

---

## 🚀 Inicio rápido

```bash
source ./ew8_unix.sh

./ew_monitor.sh start        # lanza startstop en segundo plano
# (o bien `startstop` en primer plano para ver el estado interactivo)
```

Parada:

```bash
./ew_monitor.sh stop
```

> Esta distribución **no incluye el binario `stop` / `quakessterminate`**. `./ew_monitor.sh stop`
> envía `SIGTERM` al proceso `startstop`, que detiene ordenadamente todos los módulos
> (`Earthworm shutdown complete`).

### Control de servicios

```bash
./ew_monitor.sh status                    # EarthWorm + TODOS los módulos del .d, con su estado
./ew_monitor.sh list                      # igual, en tabla (módulo / binario / cfg / pid / estado)
./ew_monitor.sh start | stop | restart    # todo el stack
./ew_monitor.sh restart csnrv             # solo un módulo
```

`status` es la vista de salud: **una fila por cada módulo declarado** en la configuración de
`startstop`, con su estado (`Alive`, `Zombie`, `Dead`, `Stop`, `NoExec`, o `ausente` si no aparece
en la tabla de `startstop`) y su pid. Antes sólo listaba los procesos vivos —un módulo muerto deja
de ser hijo de `startstop`, así que desaparecía de la lista y no había forma de ver que faltaba—.
Si el motor cosecha perezoso y la primera consulta dice `Zombie`, se reintenta una vez para poder
reportar `Dead`.

`list` y los selectores salen de la configuración de `startstop` (`EW_MONITOR_CFG`, por defecto
`$EW_PARAMS/startstop_unix.d`): **no hay ninguna lista de módulos escrita en el script**. Un
`<módulo>` puede ser el binario (`csnrv`), el tallo de su `.d` (cuando binario y `.d` no se llaman
igual) o el pid. Un nombre **ambiguo** (dos módulos con el mismo binario, como las dos instancias de
`csnmags`) se rechaza mostrando los candidatos: nunca se reinicia "el primero". Las acciones por
módulo van por **pid** a los binarios de EarthWorm `restart`/`stopmodule` (la consola de `startstop`
no sirve cuando corre desacoplado, porque su `fgets(stdin)` sale sin tty) y el script **verifica**
que el módulo haya vuelto (`Alive` con pid nuevo) o muerto antes de darlo por hecho: si no lo
confirma, sale con 4 y te dice dónde mirar. `--dry-run` enseña el comando sin enviarlo.

### Verificación

```bash
# Seguir los logs
tail -f run_working_v8/log/csnloc_*.log
tail -f run_working_v8/log/csnmags_*.log

# Hipocentros en el ring (sin drenar)
ew_8.0/bin/sniffring -n HYPO_RING
```

---

### Coherencia de estaciones

`csnloc` y `csnmags` solo usan estaciones con metadata en `estaciones_107.txt`. Un pick sin
metadata queda inerte (no se localiza ni se mide): revísalo contra esa lista antes de añadir
estaciones nuevas.

---

## 🗄️ Base de datos hipocentral

La información hipocentral (eventos, orígenes, picks, magnitudes) se almacena en **PostgreSQL**
(`db/schema.sql`): la escribe en vivo `hypodb_ingest` (desde los anillos) y la consultan
`csnhypodbp`, `csnrv` y `csnstaevdisp` (historial). Los anillos siguen siendo el transporte vivo.

```bash
cp run_working_v8/params/db.env.example run_working_v8/params/db.env
./ew8_db.sh up          # instancia 0: ew8_hypodb en el puerto 5435
db/test_schema.sh
```

Cada worktree puede tener su base **aislada**: `./ew8_db.sh -i N up` levanta
`ew8_hypodb_<N>` en el puerto `5435+N`, con contenedor y volumen propios (y sin auto-arranque).
El `db.env` de ese worktree debe declarar el mismo `DB_PORT` para `up`/`psql`/`reset`; para
**detener** (`down`), **listar** (`instances`) o **borrar** (`destroy [--yes]`) una instancia no
hace falta, así que la base de un worktree ya borrado también se puede limpiar (queda marcada como
huérfana en `instances`).

Ver `docs/HYPODB.md` y `db/README.md`.

## Notas / Solución de problemas

- En este entorno SeisComP (`~/seiscomp/bin`) queda antes de `/usr/bin` y su `timeout` **no** es el de
  GNU coreutils (falla con `couldn't exec <N>`). `source ./ew8_unix.sh` antepone
  `ew_gui_tools/common/tools/gnubin` (shim a `/usr/bin/timeout`), y los scripts del repo usan siempre
  la ruta absoluta `/usr/bin/timeout` (invariante en `common check`).
- Los módulos GUI necesitan un `DISPLAY` funcional (WSLg `:0` funciona).
- Si `slink2ew` no alcanza el servidor SeedLink, inyecta un pick de prueba con `putpick` e inspecciona
  con `sniffring`.
- Consulta `AGENTS.md` para una guía de ingeniería más detallada.

---

## 📄 Licencia y agradecimientos

Basado en el [EarthWorm Seismic System](http://earthwormcentral.org/) 8.0, con herramientas propias
desarrolladas para monitoreo y análisis sísmico.
