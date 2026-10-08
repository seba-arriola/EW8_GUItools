# EW8_GUItools: sistema portátil de procesamiento sísmico EarthWorm 8

**EW8_GUItools** es un entorno portátil de adquisición, procesamiento en tiempo real y análisis de
datos sísmicos, construido sobre **EarthWorm 8.0** y ampliado con interfaces gráficas (GUI) propias
y módulos de procesamiento sismológico (`ew_gui_tools`).

La asociación de fases y la primera localización las hace el módulo nativo **`csnloc`**
**localmente**, sin Kafka ni GLASS3 (ver `ew_gui_tools/csnloc/README.md`). Los eventos resultantes
alimentan el cálculo de magnitud y su visualización.

El camino histórico con el asociador **neic-glass3** sobre **Apache Kafka**
(`ew2glass → GLASS3 → glass2ew`) queda como **legacy deshabilitado**; ver la sección
"Legacy: GLASS3 + Kafka" al final.

---

## Arquitectura

```
SeedLink (10.54.217.9:18000)
        │  slink2ew
        ▼
   SLINK_RING ──────────────► wave_serverV (16022) ──► csntvp / csnhypodbp
        │  pick_FP
        ▼
   PICK_RING ──────────────► csntvp / csnhypodbp (re-picks manuales)
        │  csnloc (asociador + localizador local)
        ▼
   HYPO_RING (TYPE_HYP2000ARC) ──► csnmags_toy  (ML / Mwp)
                               ──► csnhypodbp  (GUI)
                               ──► csnrv       (vista regional)
```

> El camino legacy `ew2glass → Kafka (glass3_input_topic/glass_locations) → GLASS3 → glass2ew`
> también escribía `HYPO_RING` y está deshabilitado; ver "Legacy: GLASS3 + Kafka".
>
> `earthworm_picks` (en `ew2openapi.d`) pertenece al módulo obsoleto `ew2openapi` y **no** se usa.

---

## 📂 Estructura del proyecto

```text
EW8_GUItools/
├── earthworm_8.0/          # Instalación base de EarthWorm 8
├── ew_gui_tools/           # Módulos y herramientas GUI propias
│   ├── csnmags/            # Cálculo de magnitud (csnmags_toy)
│   ├── glass2ew / ew2glass # (legacy) Puentes Kafka ↔ EarthWorm
│   ├── csnhypodbp/         # Visor de hipocentros / re-picker
│   ├── csnrv/              # Visor regional
│   ├── csntvp/             # Visor de trazas / picker manual
│   ├── csnstaevdisp/       # Visor de estaciones y eventos
│   ├── csnloc/             # Asociación de fases + localización (reemplazo local de GLASS3)
│   ├── ew_controller/      # Panel de control de startstop
│   ├── _legacy_atwc/       # Herramientas WC/ATWC archivadas (referencia histórica)
│   └── libsrc/             # Legacy WC/ATWC (sin consumidores activos)
├── glass3/                 # (legacy) Código + runtime del broker GLASS3
├── kafka/                  # (legacy) Configuración de Kafka KRaft (server.properties)
├── kafka-data/             # (legacy) Datos persistentes de Kafka (ignorado por git)
├── kafka-logs/             # (legacy) Logs operativos de Kafka (ignorados por git)
├── run_working_v8/         # Runtime de EarthWorm (params, log, tanks)
├── ew8_unix.sh             # Inicialización del entorno
├── deploy_portable.sh      # Copia portable incremental (rsync) a otra máquina
├── kafka_monitor.sh        # (legacy) Control del servicio Kafka
├── glass_monitor.sh        # (legacy) Control del servicio GLASS3
└── ew_monitor.sh           # Control del servicio EarthWorm
```

---

## Requisitos

- Linux x86_64 (probado en WSL2/WSLg).
- `gcc`, `make`, `pkg-config`, librerías de desarrollo GTK3.
- Acceso al servidor SeedLink `10.54.217.9:18000` (sin él no fluyen formas de onda ni picks; el
  resto de la cadena sigue funcionando).
- (Legacy GLASS3 + Kafka: Java 11+, Apache Kafka 3.9.0 en `/opt/kafka` (KRaft) y `librdkafka` 2.3.0.)

---

## ⚙️ Preparación del entorno

Carga siempre el entorno antes de compilar o ejecutar:

```bash
source ./ew8_unix.sh
```

Configura EarthWorm (`EW_HOME`, `EW_VERSION`, `EW_PARAMS`, `EW_LOG`, `PATH`). Además, por
compatibilidad, define las variables legacy `GLASS_*`/`KAFKA_*` (ver "Legacy: GLASS3 + Kafka").
Consulta `AGENTS.md` para la tabla completa de variables.

---

## 🛠️ Compilación

Recompila todos los módulos propios (cada uno instala su binario en `earthworm_8.0/bin/`):

```bash
source ./ew8_unix.sh
for dir in ew_gui_tools/csnmags ew_gui_tools/csnhypodbp ew_gui_tools/csnrv \
           ew_gui_tools/csntvp ew_gui_tools/csnstaevdisp ew_gui_tools/ew_controller \
           ew_gui_tools/csnloc; do
  [ -d "$dir" ] && (cd "$dir" && make clean && make)
done
```

(Los binarios de GLASS3 ya están compilados en `glass3/neic-glass3/dist/`; recompilarlos requiere
volver a ejecutar CMake con el prefijo actual. Ver "Legacy: GLASS3 + Kafka".)

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
./ew_monitor.sh {status|start|stop|restart}
```

> Los scripts `kafka_monitor.sh` / `glass_monitor.sh` son **legacy** (ver al final).

### Verificación

```bash
# Seguir los logs
tail -f run_working_v8/log/csnloc_*.log
tail -f run_working_v8/log/csnmags_toy_*.log

# Hipocentros en el ring (sin drenar)
earthworm_8.0/bin/sniffring -n HYPO_RING
```

---

## 📦 Despliegue portable (otra máquina)

Genera una copia autónoma y **con rutas relativas** en `~/ew8portable` (o `--dst DIR`):

```bash
./deploy_portable.sh                 # incremental (rsync); purga obsoletos
./deploy_portable.sh --dry-run -v    # muestra lo que haria sin escribir
./deploy_portable.sh --verify-only   # valida un destino ya existente
```

- Copia **solo** el whitelist: binarios necesarios, params activos, `grids/` y tablas de tiempos de
  viaje (`iasp91.tbl/.hed`).
- Es **idempotente**: una segunda corrida sin cambios no transfiere nada (`cambiados=0`).
- `wave_serverV.d` se **regenera** con las rutas de tanks relativas (`../tanks/…`); ninguna config
  conserva rutas absolutas `/home/...`.
- `hyp2000_ring.d` y `nlloc_ring.d` también se **regeneran** con rutas relativas (`../../tmp/…`).
  El refinamiento NLLoc necesita los datos de `tmp/nlloc_ring/` (grillas 3D de tiempos) en el
  destino; el deploy **no** los copia (hay que copiarlos aparte o regenerarlos con
  `ew_gui_tools/nlloc_ring/mk_nll_grids_3d.py`).
- Excluye `_legacy_atwc/`, `response/`, `hyp2000_output/`, respaldos, logs y tanks. El **estado en
  vivo** de los módulos (`*.state`, `*.ndx`, `*.queue`) se excluye de la copia pero queda
  **protegido** de la purga: `slink2ew`/`pick_FP` lo recrean mientras operan y borrarlo les quita
  la posición de SeedLink y el índice de picks.
- Por defecto el portable es un **espejo completo**: incluye `ew_monitor.sh` (copia literal del
  repo) y los params de replay (`startstop_replay.d`, `tankplayer.d.tmpl`), para que no diverjan
  en silencio. Opciones: `--no-monitor` y `--no-replay` (los excluyen) y `--no-delete` (no purga
  obsoletos). `--with-monitor`/`--with-replay` se aceptan por compatibilidad y ya no cambian nada.

En la máquina destino basta copiar la carpeta completa y arrancar:

```bash
cd ew8portable
source ./ew8_unix.sh
startstop
```

Requisitos en el destino: Linux x86_64, `rsync`, y `GTK4` + `libadwaita` + `DISPLAY` si se usan las
GUIs. El entorno portable **no** define las variables legacy `GLASS_*` / `KAFKA_*`.

### Coherencia de estaciones

`csnloc` y `csnmags_toy` solo usan estaciones con metadata en `estaciones_107.txt`. Un pick o Tank
sin metadata queda inerte (no se localiza ni se mide). Para detectarlo y eliminarlo:

```bash
./tank_tools/prune_orphan_stations.sh --dry-run              # informa (no modifica)
./tank_tools/prune_orphan_stations.sh --only pick,tanks --apply
```

Tras podar, mueve los `.tnk`/`.inx` huérfanos y reinicia EarthWorm:

```bash
./tank_tools/align_channels.sh --only tanks \
    --prune-orphans run_working_v8/tanks/_orphan_removed --apply
```

---

## Notas / Solución de problemas

- `timeout` puede estar ensombrecido por SeisComP (`~/seiscomp/bin/timeout`); **no** es el `timeout`
  de GNU coreutils y falla con `couldn't exec <N>`. Usa `/usr/bin/timeout` en los scripts.
- Los módulos GUI necesitan un `DISPLAY` funcional (WSLg `:0` funciona).
- Si `slink2ew` no alcanza el servidor SeedLink, inyecta un pick de prueba con `putpick` e inspecciona
  con `sniffring`.
- Consulta `AGENTS.md` para una guía de ingeniería más detallada.

---

## Legacy: GLASS3 + Kafka (deshabilitado)

El camino histórico `ew2glass → Kafka → glass-broker-app (neic-glass3) → glass2ew` está
**deshabilitado** en `startstop_unix.d` / `startstop_replay.d` (la asociación+localización la hace
`csnloc`). **No se borró nada**: se conservan directorios, configs, scripts y variables.

### Cómo reactivarlo

1. `source ./ew8_unix.sh` (define `GLASS_*`/`KAFKA_*`).
2. En `run_working_v8/params/startstop_unix.d` (y `startstop_replay.d`): **descomentar**
   `Process "ew2glass ew2glass.d"` y `Process "glass2ew glass2ew.d"`, y **comentar**
   `Process "csnloc csnloc.d"` (no pueden convivir: ambos escriben `HYPO_RING`).
3. IDs ya presentes en `run_working_v8/params/earthworm.d`: `MOD_EW2GLASS 154`, `MOD_GLASS2EW 155`.
4. Arrancar en orden: `./kafka_monitor.sh start` → `./glass_monitor.sh start` → `./ew_monitor.sh start`.
   Parar en orden inverso.
5. Recompilar los puentes si hace falta:
   ```bash
   for dir in ew_gui_tools/ew2glass ew_gui_tools/glass2ew; do (cd "$dir" && make clean && make); done
   ```
6. Verificación:
   ```bash
   /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server localhost:9092 --topic glass_locations
   tail -f run_working_v8/log/ew2glass_*.log
   tail -f run_working_v8/log/glass2ew_*.log
   tail -f glass3/run_glass/logs/broker_rt_*.log
   ```

### Control de servicios (legacy)

```bash
./kafka_monitor.sh {status|start|stop|restart}
./glass_monitor.sh {status|start|stop|restart}
```

### Detalles de Kafka (legacy)

- Modo KRaft, sin ZooKeeper, corre como el usuario local (sin `sudo`).
- Datos: `kafka-data/` (persistente, dentro del proyecto). Logs: `kafka-logs/`.
- Configuración: `kafka/config/server.properties` (`log.dirs=<proyecto>/kafka-data`).
- Temas: `glass3_input_topic` (picks) y `glass_locations` (eventos).
- `kafka_monitor.sh` formatea KRaft en la primera ejecución y crea los temas.
- `ew2glass.d` viene con `Debug 1` (imprime cada pick a stdout); usa `Debug 0` para reducir la salida.

> **No** coloques `log.dirs` en `/tmp`: se borra al reiniciar y obliga a reformatear.

---

## 📄 Licencia y agradecimientos

Basado en el [EarthWorm Seismic System](http://earthwormcentral.org/) 8.0 y el asociador
[neic-glass3](https://github.com/usgs/neic-glass3), con herramientas propias desarrolladas para
monitoreo y análisis sísmico.
