# ew_8.0/ — instalación EarthWorm 8 vendorizada (subconjunto)

Este directorio es la **instalación base de EarthWorm 8** que el proyecto necesita para
compilar y operar sus módulos. **No es un árbol de fuentes**: son artefactos precompilados
(headers, librerías estáticas y los binarios de fábrica que se ejecutan), tomados del
directorio de instalación y **versionados** para que un clon/worktree nuevo funcione sin
depender de nada externo al repo.

- **Versión**: EarthWorm 8.0 (`release_notes.v8.0`).
- **Origen**: instalación prebuilt existente en la máquina de desarrollo (hoy conservada
  fuera del repo como *Legacy*).
- **Fecha de build de los artefactos**: 2026-08-07 23:51–23:52.
- **Licencia**: MIT (`Copyright (c) 2022 Seismic Software`). Ver el `LICENSE` de EarthWorm en
  la copia Legacy.

## Qué se versiona

| Ruta | Contenido |
|---|---|
| `include/` | headers de EarthWorm (se incluyen con `-I$(EW_HOME)/$(EW_VERSION)/include`) |
| `lib/libew_mt.a` | librería multihilo de `libsrc` (la enlazan **todos** los módulos) |
| `lib/libmseed2.a` | libmseed 2.13 (la enlazan `csnmags`, `csnwphase`, `csnhypodbp`) |
| `lib/rw_mag.o` | lectura/escritura de `TYPE_MAGNITUDE` (`csnmags`, `csnrv`, `csnhypodbp`, `hypodb`) |
| `bin/` | binarios de **fábrica** que el sistema ejecuta: `startstop`, `status`, `restart`, `stopmodule`, `slink2ew`, `pick_FP`, `sniffring`, `putpick` |

## Qué **no** se versiona

- Los binarios que compilan **nuestros** módulos (`csnloc`, `csnhypodbp`, `csnrv`, …): los
  instala `make install` en `ew_8.0/bin/` y están en `.gitignore` (§3b).
- `src/` (~811 MB), `tools/`, `doc/`, `demo/`, `images/`, `params/`, `environment/`,
  `python/`, `cicd/`, `debian/`, `rpm/`, `include_cpp/`, `include_win/`: no intervienen en la
  compilación ni en la operación de nuestros módulos.
- `lib/` no se necesita en *runtime*: los binarios y los módulos son estáticos respecto a
  EarthWorm (solo `libc`/`libm`/`libpthread` del sistema). Solo se usa al **enlazar**.

## Cómo se regenera (si algún día hace falta)

EarthWorm no se construye con un único `make`:

1. `source environment/ew_linux.bash && cd src && make -s unix` compila `buildinfo`, las
   librerías y `seismic_processing`; **no** los binarios que usamos.
2. Cada binario tiene su propio `makefile.unix` (`src/system_control/startstop_unix/`,
   `src/data_exchange/slink2ew/`, `src/diagnostic_tools/sniffring/`, `src/eew/pick_ffp/`, …)
   y hay que invocarlos uno por uno; `pick_FP` ni siquiera está en el build de `src/eew`.
3. Requiere `gcc`, `g++`, `gfortran` y `libtirpc`/`libtirpc-devel` (XDR/RPC).

`src/libsrc/makefile.unix` es quien arma `lib/libew_mt.a`.

## Nota sobre el backend TCP de ondas

El backend TCP (`wave_serverV`) se **eliminó** del código, así que los objetos del cliente
(`ws_clientII.o`, `socket_ew.o`, `socket_ew_common.o`) ya no están en `lib/`. En `include/`
siguen los headers `ws_clientII.h`/`socket_ew.h` (recortarlos sería cosmético).
