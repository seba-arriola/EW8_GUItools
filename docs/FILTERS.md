# Filtros de la vista de ondas (`csnhypodbp`, sub-pestaña `picking`)

Documento de referencia de los presets de filtro, sus atajos de teclado y el
procesamiento STA/LTA. El objetivo que manda sobre todo lo demás es **no
desplazar el instante de la picada**: el pick se toma del clic sobre la traza y
se manda tal cual al localizador.

---

## 1. Por qué fase cero

El filtro es **solo de visualización**: `locate` envía `ewgui_trace_raw()` y el
pick guardado es `clicked_time` (la x del clic mapeada a tiempo). Pero si el
dibujo usa un filtro causal, el analista ve el onset **retrasado** por el
retardo de grupo y pica tarde.

Retardo de grupo en continua de un Butterworth causal (`τ(0) = a1/ωc`):

| Orden | fc = 1 Hz | a 100 Hz | Sesgo con Vp = 6 km/s |
|---|---|---|---|
| 2 | ≈ 0.22 s | ≈ 22 muestras | ≈ 1.3 km |
| 4 | ≈ 0.42 s | ≈ 42 muestras | ≈ 2.5 km |

Y cerca de la frecuencia de corte es **peor**: la máxima dispersión de fase está
en el borde de banda (Smith, *Phase Distortion at Passband Edges*). SeisComP lo
palia con un `timeCorrection` estático (`-0.8 s` en `scautopick`), que es una
aproximación.

`FilterPhase zero` (default) aplica el filtro **forward + backward**
(`ewgui_filter_iir_zp`), con fase neta cero: el pico de una wavelet queda en la
misma muestra. `FilterPhase causal` deja el comportamiento histórico (una
pasada), útil solo para comparar.

### Qué le hace la fase cero a la respuesta en amplitud

La respuesta combinada es `|H(f)|²`, es decir **el orden se duplica**:

- en `fc` la ganancia es `0.5` (**-6 dB**, no -3 dB);
- el punto de -3 dB se corre a `fc·(√2−1)^(1/2N)` ⇒ ≈ **0.80·fc** para orden 2
  (LP; en HP, simétrico hacia arriba);
- a `2·fc`, orden 2 ⇒ `1/(1+2⁴)` ≈ **-24.6 dB** (la pendiente de un 4.º orden).

Por eso los presets recomendados usan **orden 2**: con fase cero ya equivalen en
amplitud al `BW(4, …)` de SeisComP, que es el default operativo. Un `orden 4`
con fase cero es un 8.º orden efectivo: mucho ringing y onsets "ensanchados".

### Resguardos numéricos y de gaps

- `fc · n / fs < 0.5` (menos de medio ciclo en toda la traza) ⇒ **no-op**. Con
  `BP 0.001 12` el high-pass cae aquí (los coeficientes biquad a 0.001 Hz están
  al borde de la cancelación catastrófica) y el resultado equivale a un LP 12.
- Los gaps (`INT_MAX`) se conservan y **cada tramo contiguo se filtra por
  separado**, con padding por reflexión impar: el ringing no cruza un hueco.
- Un tramo menor que `2·pad` (con `pad = max(9, 3·(orden+1))`) solo se demean.
- La salida se satura a `int32_t`; `raw` nunca se toca (`memcmp` en los tests).

---

## 2. Configuración (`.d`)

```
FilterPhase     zero          # zero (default) | causal
FilterMenu      f             # abre la ventana Filters…
FilterRaw       0             # vuelve a la traza cruda

Filter [<nombre>] <HP|LP|BP> <tecla> <f1> [<f2>] <orden>
StaLta  <tecla> <sta_s> <lta_s> <on> <off> [<corr_s>]
```

- Tecla: un carácter ASCII imprimible, sin distinguir mayúsculas. **`p` y `s`
  están reservadas** por el picado de fases. Sin duplicados entre categorías.
- `orden ∈ {2, 4}`. `f1 > 0`; en BP `f1 < f2`. Frecuencias sobre Nyquist se
  recortan al aplicar (como antes).
- Las líneas son **aditivas sobre los defaults**: una tecla existente reemplaza
  ese preset. Un duplicado *dentro del mismo `.d`* es error de configuración
  (`ReadConfig` devuelve -1 y nombra la línea).
- El nombre es opcional: si el 2.º token es `HP`/`LP`/`BP`, el 1.º es el tipo.
- La config vigente se ve con `csnhypodbp --print-config csnhypodbp.d`
  (`FilterPhase`, `FilterMenu`, `FilterRaw`, `Filters`, `Filter …`, `StaLta …`).

### Defaults

| Tecla | Nombre | Banda | Orden | Uso |
|---|---|---|---|---|
| `1` | `P_local` | BP 1.0–20 Hz | 2 | P local (< 200 km) |
| `2` | `P_regional` | BP 0.7–10 Hz | 2 | P regional (200–1000 km) |
| `3` | `P_teleseismic` | BP 0.5–2 Hz | 2 | P telesísmico (> 1000 km) |
| `4` | `S_local` | BP 0.5–8 Hz | 2 | S local (< 200 km) |
| `r` | `HP` | HP 1.0 Hz | 2 | uso general |
| `k` | `LP` | LP 0.1 Hz | 2 | uso general |
| `d` | `BP` | BP 0.001–12 Hz | 4 | uso general (ver guarda numérica) |
| `f` | — | — | — | abre la ventana `Filters…` |
| `0` | — | — | — | vuelve a `Raw` |
| `t` | — | — | — | STA/LTA on/off |

**No hay selección automática por distancia**: cada filtro recomendado tiene su
propia tecla y el analista elige según la distancia del evento. El HUD de la
barra inferior muestra siempre qué quedó activo, p. ej.
`P_regional BP 0.70-10 Hz · n=2 (2)`.

---

## 3. Evidencia de las bandas

Advertencia: **no existe una tabla revisada "frecuencia vs distancia" para
picking de precisión**. Lo que hay son recomendaciones explícitas (NMSOP/GFZ),
defaults operativos documentados (SeisComP/ObsPy) y papers que comparan el efecto
del filtro en el onset. Las bandas de abajo son, por tanto, convención
documentada; el error temporal que introducen, en cambio, está medido.

| Fase / campo | Banda | Fundamento |
|---|---|---|
| P local | 1–20 Hz | NMSOP IS 11.4 §2.5/§3.1 (Diehl et al. 2012): con BB en velocidad, HP 2.º orden a 1 Hz para simular SP. Detectores locales publicados usan BP 2–10 / 2–15 Hz (Küperkoch et al. 2010, GJI 181:1159). |
| P regional | 0.7–10 Hz | Default operativo SeisComP `BW(4,0.7,2)` (`scautopick` / filter grammar) + contenido regional. ObsPy usa 1–20 Hz con STA 0.5 / LTA 4–10. |
| P telesísmico | 0.5–2 Hz | Detector telesísmico de Murdock & Hutt (1983, BSSA 73:1173): dispara en 0.5–2 Hz e **inhibe** con 3–8 Hz. |
| S local | 0.5–8 Hz | Es la fase con menos evidencia de banda propia. Diehl et al. (2009, BSSA 99:1906) pican S local con STA/LTA + polarización + AR-AIC porque la S llega **dentro de la coda de la P**; NMSOP IS 11.4 §3.1 recomienda Wood-Anderson (fcl 1 Hz) + integración + rotación a RT. La banda baja respecto de P por atenuación (Qs < Qp) y por el contenido de la coda. |

Referencia cuantitativa del sesgo (telesísmico, BB vs SP): σ de la diferencia de
pick 0.14/0.38/1.14 s para calidades 1/2/3 y un retardo sistemático de
+0.06 ± 0.01 s del SP causal estrecho (Univ. Washington, FARM97).

---

## 4. STA/LTA (solo visual)

- CF de energía: `media(x² en STA) / media(x² en LTA)`, con `LTA ≥ STA`;
  `cf = 0` antes de llenar la LTA y en los gaps (que reinician el detector).
- Se calcula sobre la **traza dibujada** (filtrada, fase cero), así queda
  alineada 1:1 con el eje x, y se recalcula al cambiar de filtro.
- La curva se superpone atenuada y las marcas de disparo se dibujan en
  `t_disparo + corr` (default `-sta/2`).
- **El disparo no es el onset**: la STA tarda en llenarse y el cruce del umbral
  depende del SNR (a mayor SNR, antes cruza). Por eso la marca es una
  *estimación*, se configura con `corr_s` y **nunca crea ni modifica picks**.
  SeisComP hace lo mismo con su `timeCorrection` estático.
- Defaults: `STA 0.5 s`, `LTA 10 s` (LTA 10–20× STA), `on 3.0`, `off 1.5`
  (tutorial ObsPy, que cita Withers et al. 1998 y Trnkoczy 2012 NMSOP IS 8.1).

---

## 5. Teclado y foco

- Los atajos actúan **solo** con la pestaña raíz *Procesar* y la sub-pestaña
  `picking` activas. En *magnitudes* no se consumen (el filtro no aplica y el HUD
  no está visible).
- **Guarda de foco**: si el foco está en un widget editable (`depth`, buscador,
  puertas de magnitud), ninguna tecla se consume. Sin esto, escribir `p` en
  `depth` entraba en modo PICAR.
- Orden de despacho: `Shift+flechas/0` → `Esc` → `p/P` → `s/S` → filtros.
- Toggle: la tecla del filtro activo vuelve a `Raw`; otra tecla cambia.

La ayuda (`☰ → Help → Commands…`) se genera de la config vigente
(`csnhypodbp_filtercfg_help_text`), no de una lista hardcodeada.

---

## 6. Dónde vive cada cosa

| Qué | Archivo |
|---|---|
| Filtro causal y de fase cero, guardas y padding | `common/src/ewgui_dsp.c`, `common/include/ewgui/dsp.h` |
| `EwFilterParams.zerophase` y despacho | `common/src/ewgui_wave.c`, `common/include/ewgui/wave.h` |
| CF y disparos (puro) | `common/src/ewgui_stalta.c`, `common/include/ewgui/stalta.h` |
| Presets, parseo, etiquetas, ayuda (puro) | `csnhypodbp/csnhypodbp_filtercfg.{h,c}` |
| Diálogo, atajos, HUD, dibujo del CF | `csnhypodbp/csnhypodbp_gui.c`, `csnhypodbp/csnhypodbp_main.c` |
| Tests | `common/tests/test_dsp.c`, `test_wave.c`, `test_stalta.c`, `csnhypodbp/test_filtercfg.c`, `test_config.sh`, `test_ui_invariants.sh` |

---

## 7. Referencias

- Diehl, T., et al. (2012). *Tutorial for consistent phase picking at local to
  regional distances*. NMSOP-2, IS 11.4. https://doi.org/10.2312/gfz.nmsop-2_is_11.4
- Küperkoch, L., et al. (2010). *Automated determination of P-phase arrival
  times at regional and local distances*. GJI 181:1159.
- Diehl, T., et al. (2009). *Automatic S-wave picker for local earthquake
  tomography*. BSSA 99:1906.
- Murdock, J. & Hutt, C. (1983). *A teleseism-specific detection algorithm*.
  BSSA 73:1173.
- Baer, M. & Kradolfer, U. (1987). *An automatic phase picker for local and
  teleseismic events*. BSSA 77:1437.
- Withers, M., et al. (1998). *A comparison of select trigger algorithms*.
  BSSA 88:95.
- Trnkoczy, A. (2012). *Understanding and parameter setting of STA/LTA trigger
  algorithm*. NMSOP-2, IS 8.1.
- SeisComP: `scautopick` y *Filter Grammar*.
  https://docs.gempa.de/seiscomp/current/apps/scautopick.html ·
  https://docs.gempa.de/seiscomp/current/base/filter-grammar.html
- ObsPy: `Trace.filter` (`zerophase`) y tutorial de triggers.
  https://docs.obspy.org/tutorial/code_snippets/filtering_seismograms.html
- SciPy: `filtfilt` / `sosfiltfilt` (orden doble, fase cero, `padlen`).
  https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.filtfilt.html
- Smith, S. W. *Introduction to Digital Filters*: forward-backward filtering,
  phase distortion at passband edges, group delay.
  https://www.dsprelated.com/freebooks/filters/
