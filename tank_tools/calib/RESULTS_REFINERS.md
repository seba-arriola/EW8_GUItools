# Calibración de los refinadores (`hyp2000_ring` / `nlloc_ring`)

El catálogo `tests_soluciones_publicadas.dat` es una **referencia para mirar a ojo**, no una métrica
absoluta. Un refinador no decide qué eventos aparecen (eso ya lo hizo `csnloc`): lo que se mide es
**cuánto se acerca** la solución a la publicada. Por eso se usan **tres familias de métricas** y se
decide mirando las tres, no una:

1. **epicentral**: cuántos eventos del archivo quedan a ≤25/≤50/≤100 km y la mediana del desvío;
2. **profundidad**: mediana de `|dz|` contra la **profundidad publicada** (no contra el crudo);
3. **ajuste**: RMS medio de la solución refinada.

Mirar sólo la familia 1 lleva a conclusiones falsas: hay sets que "recuperan" eventos a ≤100 km
simplemente bajando el peso de las estaciones lejanas, con un RMS de 1.71 frente a 0.68.

- **Suelo**: `tmp/refine/arcs/` — **1777 ARC de 388 slugs**, congelado con el `csnloc.d` de
  producción (`AssocWindowSec 45.0`, sha256 `096ed77d…`). Coincide con el barrido de `csnloc`
  (388 tanks con solución / 1777 soluciones), lo que valida el suelo.
- **Crudo** (lo que entra al refinador): 251 a ≤25 km, 350 a ≤50, 377 a ≤100, mediana 19.5 km,
  `|dz|` vs publicado 27.9 km, RMS 1.452 s.

---

## 1. `hyp2000_ring` — 388 eventos

Barrido completo en `tank_tools/calib/hyp2000_variants.json` (27 sets); reportes A+B por set en
`tmp/refine/hyp2000/val/<tag>/`.

| set | ≤25 km | ≤50 km | ≤100 km | mediana (km) | \|dz\| publ. (km) | RMS (s) |
|---|---:|---:|---:|---:|---:|---:|
| *crudo* | 251 | 350 | 377 | 19.5 | 27.9 | 1.452 |
| **h_d15_z20** (husen + `DAM 15.` + `ZTR 20.0 F`) | **302** | **355** | 370 | **11.9** | 16.5 | 0.688 |
| h_damp15 / h_husen_d15 (husen + `DAM 15.`) | 301 | 354 | 370 | 12.0 | 17.7 | **0.680** |
| h_ztr20 (husen + `ZTR 20.0 F`) | 300 | 354 | 369 | 12.1 | 17.9 | 0.696 |
| h_husen (`CRH 1 'chile_1d.crh'`) | 299 | 353 | 369 | 12.1 | 19.6 | 0.688 |
| hak135_d15_z20 | 295 | 348 | 368 | 12.1 | 20.8 | 0.667 |
| h_d2far250 (`D2FAR 250`) | 295 | 356 | **373** | 12.2 | 20.0 | **1.711** ⚠ |
| h_ak135 | 293 | 349 | 370 | 12.8 | 21.3 | 0.673 |
| **hb_d15_z20** (bandas + `DAM 15.` + `ZTR 20.0 F`) | 292 | 346 | 370 | 12.4 | **13.3** | 0.705 |
| hb_d15 (bandas + `DAM 15.`) | 291 | 345 | 369 | 12.4 | 14.0 | 0.717 |
| hb_ak135_d15 | 292 | 348 | 368 | 12.1 | 21.8 | 0.671 |
| h_bandas (6 bandas 3D + `MUL T 3` + `NOD`) | 288 | 345 | 369 | 12.9 | 14.4 | 0.722 |
| h_ztr0 (`ZTR 0.0 F`) | 285 | 345 | 369 | 13.1 | 16.3 | 0.759 |
| h_minph6 (`MinPhases 6`) | 284 | 341 | 362 | 12.7 | — | 0.662 |
| h_minph3 / h_moddef1 / h_pos1.65…1.85 / h_let / h_ztr5 | 288-299 | 345-353 | 369 | 12.1-12.9 | 14.4-19.6 | 0.69-0.72 |
| h_maxrms1 (`MaxRMS 1.0`) | 41 | 51 | 60 | 37.6 | 21.1 | 2.362 |

### Lectura

- **El refinador mejora con cualquier modelo razonable**: 251→285-302 a ≤25 km, mediana 19.5→11.9-13.1,
  `|dz|` 27.9→13.3-21.3, RMS 1.452→0.67-0.76. **La ganancia viene de re-localizar, no del modelo.**
- **Las dos familias de métricas discrepan de forma sistemática**:
  - el **modelo 1D único** (`chile_1d.crh`) da mejor **epicentro** (302 vs 292 a ≤25 km; 11.9 vs
    12.4 de mediana) y mejor RMS;
  - las **6 bandas derivadas del 3D** dan mejor **profundidad** (13.3 vs 16.5 km, 3.2 km) a cambio de
    10 eventos menos a ≤25 km.
  Esto **corrige** la conclusión anterior ("gana el 1D"): era un artefacto de mirar sólo el epicentro.
  El banding **sí aporta** información (la profundidad), que es justo lo que un modelo regionalizado
  debe aportar. Lo que aporta poco es el detalle de la asignación (`MUL T 1` ≡ `MUL T 3`) y el
  promedio por caja.
- **`DAM` y `ZTR` sí mueven la aguja** (eran los ejes nunca tocados): `DAM 7.`→`15.` mejora el
  epicentro (299→301) y la profundidad (19.6→17.7); `ZTR 10.0`→`20.0` mejora lo mismo; juntos dan el
  mejor set 1D (302 / 11.9 / 16.5).
- **`h_d2far250` es una trampa**: mejora el recuento a ≤100 km (373, el mejor) y a ≤50 (356) pero con
  **RMS 1.711** (vs 0.68). Bajar el peso de las estaciones lejanas suelta la solución: "aparece más"
  sin ser mejor. Descartado.
- **`MaxRMS 1.0` es destructivo** (descarta 1648 de 1777 ARC, deja 60 eventos). **`MinPhases 6`**
  descarta 382 ARC y pierde 7 eventos. **`MinPhases 3`** no aporta.
- **`POS` (Vp/Vs) no tiene ningún efecto**: 1.65/1.70/1.75/1.80/1.85 dan resultados idénticos, y
  `hycrh.for:14` (`FORMAT (2F5.2)`) confirma que **el `.crh` no admite columna de Vs**. Con modelos
  sólo-Vp el eje Vp/Vs no es accionable en hyp2000 (en NLLoc sí lo es).
- `LET`, `MUL` y `ZTR 5.0` no cambian nada medible.

### Set recomendado

| prioridad | set | `.hyp` |
|---|---|---|
| **por defecto** | `h_d15_z20` | `CRH 1 'chile_1d.crh'` + `ZTR 20.0 F` + `DAM 15. 30. 0.5 0.9 0.012 0.02 0.6 50. 800.` |
| **si manda la profundidad** | `hb_d15_z20` | lo anterior + bloque `CRH 1..6` + `MUL T 3` + los 6 `NOD` |

`h_d15_z20` es **estrictamente mejor** que lo que estaba desplegado (`h_husen`) en las tres familias,
así que se aplicó en `run_working_v8/params/hyp2000_ring.hyp` y en `tmp/hyp2000_ring/`.

---

## 2. `nlloc_ring`

### 2.1 Defecto encontrado: la banda elegida no tenía las estaciones del evento

`select_model` (`nlloc_ring.c:412-424`) se quedaba con la **primera** banda cuyo bbox contiene el
epicentro. Las bandas 3D se **solapan ~4°**, y las grillas de tiempos de cada banda se generan **sólo
con las estaciones dentro de su bbox ± 0.5°** (`mk_nll_grids_3d.py:158-160`). Consecuencia: un evento
en el borde sur de una banda puede tener **todas** sus estaciones en la banda vecina → 0 tiempos de
viaje → 0 observaciones → el evento se descartaba **en silencio** (varios `return -1` sin log).

Evidencia (determinista, no intermitente):

| ARC | epicentro | banda elegida (antes) | estaciones en esa grilla |
|---|---|---|---|
| `test102_01.arc` | −29.65 / −71.31 | `N22-30` (primera) | **0 de 8** → fallaba |
| `test102_00.arc` | −31.2 / −71.0 | `N26-34` (primera) | 8 de 8 → funcionaba |

**Corregido** en `select_model`: entre las bandas que contienen el epicentro gana **la que cubre más
estaciones del evento** (`band_covers`), y se añadieron los logs que faltaban para los caminos
silenciosos. Verificado: `test102_01.arc` pasa de fallar a `rc=0` con
`banda N26-34_1.5k (cubre 8/8 estaciones)`.

Efecto medido sobre 39 ARC (`test10[0-9]`, serial): **31/39 → 35/39** refinados.

### 2.2 Lo que sigue fallando

Con la cobertura arreglada quedan eventos donde NLLoc **no converge**: el log (ahora visible) muestra
`ARC de salida vacio` incluso con `cubre 7/7 estaciones`. Es decir, el residuo es la **búsqueda**,
no la cobertura — y `LOCSEARCH`/`LOCMETH` están explorados sólo en 3 de sus ~18 números.

Además, cuando la banda elegida cubre pocas estaciones la solución sale con menos fases
(se ve `in{nph=12} out{nph=4}`): no falla, pero **empeora**.

### 2.3 Barrido completo (1777 ARC, 388 eventos)

`--jobs 9 --arc-jobs 1` (misma condición para todos, así la comparación es válida).

| set | refinados | ≤25 km | ≤50 km | ≤100 km | mediana (km) | \|dz\| publ. | RMS |
|---|---:|---:|---:|---:|---:|---:|---:|
| *crudo* | — | 251 | 350 | 377 | 19.5 | 27.9 | 1.452 |
| **n_oct_less** (`OCT 48 24 6 …`) | 1559 | **332** | **360** | **367** | **5.6** | **6.0** | **0.209** |
| n_bandas (`OCT 96 48 6 …`, producción) | 1559 | 331 | 359 | 366 | 5.7 | 6.1 | 0.214 |
| n_gau (`LOCGAU 0.5`) | 1559 | 331 | 359 | 366 | 5.7 | 6.1 | 0.214 |
| n_minph3 (`MinPhases 3`) | 1559 | 331 | 359 | 366 | 5.7 | 6.1 | 0.214 |
| n_vpvs173 (`Vp/Vs 1.73`) | 1559 | 336 | 360 | 366 | 6.1 | 8.0 | 0.211 |
| n_vpvs185 (`Vp/Vs 1.85`) | 1559 | 326 | 357 | 366 | 7.1 | 8.9 | 0.221 |
| n_maxrms1 (`MaxRMS 1.0`) | 101 | 55 | 59 | 60 | 9.5 | 8.4 | 0.250 |
| n_fallback1d (sin bandas, 1D) | 616 | 2 | 6 | 27 | 235.9 | 70.2 | 0.975 |
| n_oct_more (`OCT 200 100 6 …`) | 30 | 0 | 0 | 0 | 336.7 | 44.0 | 0.734 |

**NLLoc es el mejor localizador de los tres, en las tres familias**:

| | crudo | hyp2000 (`hb_d15_z20`) | nlloc (`n_bandas`) |
|---|---:|---:|---:|
| ≤25 km | 251 | 292 | **331** |
| mediana epicentral | 19.5 | 12.4 | **5.7** |
| `\|dz\|` vs publicado | 27.9 | 13.3 | **6.1** |
| RMS | 1.452 | 0.705 | **0.214** |

- **Las 6 bandas 3D son imprescindibles**: el fallback 1D localiza 27 de 388 eventos y su mediana
  se va a 236 km. **`LOCSEARCH` más profundo es catastrófico** (0 eventos). **`MaxRMS 1.0`** deja 60.
- **`Vp/Vs 1.78` está bien elegido**: 1.73 recupera 5 eventos a ≤25 km pero **empeora la profundidad**
  (8.0 vs 6.1 km); 1.85 empeora todo.
- **`n_oct_less` (`OCT 48 24 6`) sale marginalmente mejor en todo** (332 vs 331, 5.6 vs 5.7, 6.0 vs
  6.1) pero **no es más rápido** (4 ARC en 31.9 s vs 32.0 s): la diferencia está dentro del ruido y no
  hay premio de coste, así que **se mantiene `OCT 96 48 6`**.
- **Escalar `LOCGRID` (factor 2) no cambia ni el resultado ni el tiempo** (bytes idénticos).

### 2.4 Cobertura de estaciones y fallos residuales — RESUELTO

Distribución de `cubre X/Y` sobre los 1679 eventos procesados por `n_bandas` **antes** del arreglo:

| cobertura | eventos | % |
|---|---:|---:|
| completa | 1101 | 65.6 % |
| parcial | 556 | 33.1 % |
| cero | 22 | 1.3 % |

Y de los 179 fallos (`ARC de salida vacio`), los 77 con traza de cobertura eran **55 parciales + 22
con cero, y NINGUNO con cobertura completa**. O sea: **la búsqueda de NLLoc no estaba mal; el
problema era geométrico** — las cajas de modelo miden ~8° de latitud y un evento puede usar
estaciones repartidas en más de 8°, así que ninguna banda podía contenerlas todas.

**Arreglo aplicado, en dos piezas:**

1. **Modelo fusionado `CHILE_4k`** (`ew_gui_tools/nlloc_ring/mk_mod_merge.py`): los 6 trozos de Potin
   a 4 km fusionados en una grilla común de 409×945×80 (124 MB; error medio de re-muestreo
   **0.019 km/s**), con **3° de relleno** por replicación del borde (lat −48.9…−15.0, lon −78.9…−61.4)
   y **108 estaciones** (frente a ~65 en las 6 bandas juntas). Se añade como **última** `ModelBand`,
   así que sólo entra cuando ninguna banda fina cubre el evento entero: a igual cobertura gana la
   primera de la lista, y las finas van arriba.
2. **`LocGridKm` (nuevo, 600 km)**: NLLoc **no acepta hipocentro semilla**, así que el volumen de
   búsqueda es la única forma de decirle dónde mirar. Con el LOCGRID de todo Chile (409×945×80) el
   octree **no converge**; con cualquier volumen más chico converge y da **exactamente la misma
   solución** (probado con 200×200, 150×150, 100×100, 50×50 y 25×25 → 1578 bytes idénticos). Ahora el
   módulo **re-centra el LOCGRID en el epicentro de entrada** (600 km de lado, recortado al alcance
   del modelo), lo que además elimina el pegado al borde que sufrían las bandas finas.

Efecto medido en los casos que fallaban o salían con fases de menos:

| ARC | antes | ahora |
|---|---|---|
| `test100_00` | 11/12 estaciones, **sin solución** | **12/12**, rc=0 |
| `test106_01` | 7/10, sin solución | **10/10**, rc=0 |
| `test3_00` | 8/8 (banda fina) | **27/27** (3.4× más observaciones) |

**Resultado medido (1777 ARC, 388 eventos, serial, `tmp/refine/nlloc_chile4k`):**

| | 6 bandas solas (antes) | + `CHILE_4k` + `LocGridKm` |
|---|---:|---:|
| refinados | 1559 | **1712** |
| sin localización | 174 | **21** |
| con solución | 386 | **388** |
| ≤25 km | 331 | **345** |
| ≤50 km | 359 | **372** |
| ≤100 km | 366 | **375** |
| mediana (km) | 5.7 | 5.7 |
| adicionales | 1173 | 1324 |

Los dos arreglos recuperan **153 de los 174 eventos** que se perdían y mejoran las tres bandas
epicentrales sin tocar la mediana. Los 21 que quedan son eventos cuyas estaciones caen fuera del
modelo de Potin (Magallanes al sur de −48.9°, Antártida, Isla de Pascua): haría falta otro modelo.

### 2.5 Coste

**~7-8 s por ARC** (4 ARC en 32 s) y **~1.6 GB de pico** por corrida. No es memoria (125 GB en la
máquina). Ni `LOCGRID` ni `LOCSEARCH` mueven el tiempo: la palanca está en otra parte y sin explorar.
Para miles de mSeed esto importa: a 8 s por evento, un mes de réplicas densas satura el módulo.

---

## 3. Hallazgos de implementación

- **`nlloc_ring` descartaba en silencio** por cobertura de estaciones (§2.1) — corregido, con logs.
- **`POS` (Vp/Vs) es inerte en hyp2000** porque el `.crh` es sólo-Vp (`hycrh.for:14`).
- **El `.crh` de bandas es una media horizontal de Vp sobre una caja de ~8°×6°**, sin Vs ni densidad
  (`mk_crh_from_3d.py:104-107`). `hytra.for:48-103` mezcla hasta 3 modelos con taper coseno, así que
  la regionalización funciona; lo grueso es cada modelo. **El siguiente paso natural es reconstruir
  los `.crh` con regiones más pequeñas** (p.ej. `--region` de ±2° alrededor de cada centro).
- **`has_geo` sólo comprobaba presencia de claves** (`offline_report.py:73`), no que los valores
  existieran: una solución con `lat: null` rompía el cruce. Corregido en ambos lados.
- Los refinadores hacen `chdir(WorkDir)`: **todo lo que reciben debe ser absoluto**.
- El modo offline de `hyp2000_ring` **no** aplica `MinPhases`/`MaxRMS` (están en el bucle de anillo);
  `nlloc_ring` sí. El arnés emula el filtro sobre el ARC de entrada.

---

## 4. Recomendación

| módulo | acción | base |
|---|---|---|
| `hyp2000_ring` | **aplicado**: 6 bandas 3D (`CRH 1..6` + `MUL T 3` + `NOD`) + `ZTR 20.0 F` + `DAM 15. …` | **decisión del usuario**: en Chile la subducción cambia a lo largo de ~4300 km N-S y un perfil 1D único arrastra ese error; se prioriza profundidad (13.3 vs 16.5 km) |
| `hyp2000_ring` | alternativa descartada: 1D (`h_d15_z20`) | mejor epicentro (302 vs 292 a ≤25 km) y mejor RMS, pero 3.2 km peor de profundidad |
| `hyp2000_ring` | mantener `MinPhases 4` / `MaxRMS 2.0` | 1.0 es destructivo; 6 pierde eventos |
| `nlloc_ring` | **corregido** `select_model` (cobertura de estaciones) | la cobertura nula bajó de ~15 % a 1.3 %; los fallos residuales son 100 % por cobertura parcial |
| `nlloc_ring` | **dejar la config como está**: 6 bandas, `Vp/Vs 1.78`, `MaxRMS 2.0`, `OCT 96 48 6`, `LOCGAU 0.2` | es el mejor de los tres en las tres familias; ninguna alternativa lo supera |

## 5. Pendiente

Ordenado por valor:

1. **Fusionar los 6 modelos 3D en uno** que cubra todo Chile y generar una sola grilla de tiempos con
   las ~90 estaciones. Es el arreglo de fondo de NLLoc: hoy 33 % de los eventos sale con fases de
   menos y ~10 % no se refina, y **todos** los fallos son por cobertura parcial o nula (§2.4).
2. **`LOCSEARCH`/`LOCMETH` completos** (los ~18 números). Ya no para los fallos —la búsqueda converge
   siempre con cobertura completa— sino para el **coste** (8 s/evento) y el ajuste fino.
3. **Reconstruir los `.crh` por bandas con regiones pequeñas** (`mk_crh_from_3d.py --region`, ±2° por
   centro) y volver a comparar: es lo que convertiría la ventaja de profundidad de las bandas en algo
   aprovechable sin perder epicentro.
4. **`csnloc`: las 9 claves ocultas** (tolerancias de asociación P/S, residuales máximos, degradación
   por RMS/gap, dedup, renúcleo) y **las 13 grillas** (`NodeKm`, capas de profundidad). El asociador
   es el insumo de todo lo demás y es la mitad menos explorada del sistema.
5. Radios/centros de los `NOD` (`450/150` siguen sin justificar por ningún análisis).
6. Barrido de `LOCQUAL2ERR` (calidad de pick → error) y `LOCGAU2`: nunca tocados, y `LOCQUAL2ERR` es
   la vía natural para usar la información de calidad de `pickS`.

---

## 6. Defecto encontrado: `hyp2000` no veia las fases S (corregido)

`csnloc` escribia la linea de fase del ARC con **la letra de fase en el campo del remark P**
(`KPRK`, offsets 13-15) y **los campos S en blanco**: tiempo S (41-45), `KSRK` (46-47), `LSWT` (49)
(`hypo_out.c:165,183-190`). Al leerlo, `hyphs.for:552` **anula el remark S** (la ventana
`STRI(42:46)` esta en blanco) y `hyloc.for:211` (`IF (KSRK.NE.'  ')`) nunca entra a la rama S.

**Medido**: el suelo tiene **1896 fases S en 802 de los 1777 ARC**; borrarlas todas dejaba los
resultados de hyp2000 **identicos byte a byte** (292/346/370, mediana 12.4, `|dz|` 13.3, RMS 0.705).
O sea: hyp2000 era **efectivamente P-only**, y eso explica de una que `POS` (Vp/Vs) fuera inerte y
por que nlloc le saca tanta ventaja (nlloc lee `p[14]` en `write_obs` y si usa `LOCMETH`'s VpVs).

**Arreglo** (`hypo_out.c`): para una S se escribe ademas el tiempo en la columna S (`%05.2f` en
41-45), `KSRK='S '`, `LSWT='0'`, y el campo P se deja en **`LPWT='4'`** (peso 0, `hycmd.for:1925`)
para que HYPOINVERSE no la meta por la rama P (`hyloc.for:204`). Se conserva la letra en `[14]`
porque es lo que leen `nlloc_ring` y las GUIs: el cambio es **aditivo**.

**Verificado**: (a) `make check` de csnloc en verde; (b) la linea queda
`[14]='S' [16]='4' [41:46]='55.00' [46:48]='S ' [49]='0'`; (c) el ARC de **salida** de hyp2000
conserva `KSRK='S '`/`LSWT='0'`. Ver §6.2: el arreglo es de **forma**, no de fondo.

### 6.1 Abierto: `POS`/`PSM` (Vp/Vs) sigue inerte

Con la S ya bien codificada, `POS 1.65`, `POS 1.85` y `PSM 1 1.50 0.` siguen dando resultados
identicos, **antes y despues** del bloque `CRH`. El codigo dice que deberia funcionar:
`hybeg.f:57` corre **antes** de los comandos (`hypoinv.for:252-274`), `hycmd.for:427` escribe
`POSM(I)=POS` y `hyloc.for:333,342` usa `PSFAC=POSM(MOD)` en el residual S. Queda **sin explicar**
y anotado como pendiente: la via para el ratio Vp/Vs de hyp2000 no esta identificada. En nlloc el
eje **si** es accionable (medido: 1.73/1.78/1.85 cambian el resultado).

### 6.2 CORRECCION: hyp2000 sigue ignorando las S. El arreglo es de FORMA, no de fondo

Comparando los ARC **refinados** del binario viejo y el nuevo (misma config, parser correcto via
`arc_header`), con 1968 ficheros comunes:

- **geometria identica: 1968/1968 (100 %)** — lat/lon/profundidad no se mueven;
- **RMS identico: 100 %**; conteo de fases identico (P=21328, S=2042 en ambos).

Lo unico que cambia son las columnas de remark que el ARC de salida **eco** del de entrada. Prueba
directa y concluyente: **`SWT 0.0` (peso S a cero) ≡ `SWT 5.0` ≡ base**, geometria 100 % identica
en los 1967 comparables y las tres con las mismas metricas (302/355/372, 11.2, 13.4).

**Conclusion**: hyp2000 **no usa las S para la solucion** — ni con la codificacion canonica ni
forzando el peso. El arreglo de `hypo_out.c` deja el ARC **canonicamente correcto** (util para el
formato y para otros consumidores), pero **no desbloquea las S en hyp2000**. Esto tambien explica
§6.1: si la S nunca entra a la inversion, `POSM` no se usa nunca, y por eso `POS`/`PSM` son inertes.

**Correccion de metodo**: la comparacion por `sha256` de los ARC refinados **no prueba** que la
solucion cambie, porque el hash incluye las lineas de fase (que se ecoan). Hay que comparar los
campos de la cabecera (`arc_header`: lat/lon/`depth_km`/`rms_sec`).

**Abierto**: la causa raiz de por que hyp2000 descarta la S. Es un hueco real — sin S no hay buena
restriccion de profundidad/distancia, y es justo donde nlloc le saca ventaja.

## 7. Threads: medido y aplicado

`NumThreads` solo paraleliza el bucle de nodos de la retroproyeccion (`backprojection.c:605-618`,
pthreads con `create`/`join` por llamada). Son **seriales** `RefineHypo` (una por candidato),
`AssembleCandidates` (DBSCAN + asignacion de fases + RMS/gap) y `find_nucleations`.

Medido sobre 25 tanks (16 nucleos fisicos / 32 hilos, Xeon Gold 6136):

| `NumThreads` | tiempo | speedup |
|---|---:|---:|
| 1 | 152 s | 1.00x |
| 4 (antes) | 86 s | 1.77x |
| **8 (aplicado)** | **71 s** | **2.14x** |
| 16 | 65 s | 2.34x |

El techo es 2.34x (~42 % serial), asi que **8 captura el 91 % de la ganancia**. Aplicado en
`csnloc.d`. La mejora grande pendiente es paralelizar `RefineHypo` sobre candidatos.

---

## 8. Cola de NLLoc: rate-limit y guardia de contenido

Estado previo (`nlloc_ring.c`): `PEND_EV` guarda el **ARC completo** de cada evento
(`qid`, clave `(136,10)`) mas `last_seen`/`last_run`; `pend_process` (:764) relanzaba NLLoc
**cada `NllIntervalSec` sobre el ARC guardado**, cambiara o no, y un update **no** reseteaba
`last_run` (:739). O sea: si un evento dejaba de recibir versiones nuevas, se seguia pagando
(~8 s de NLLoc) indefinidamente por el mismo trabajo.

Cambio aplicado:
- flag `dirty` en `PEND_EV`, con `arc_same_content()` que compara el ARC **ignorando el campo
  `version`** (178-181 Y2000, 161 antiguo), que es lo unico que cambia entre re-emisiones del
  mismo contenido;
- `pend_process` solo corre si `dirty`, y limpia el flag tras la corrida (log a `Debug >= 2`);
- `NllIntervalSec 300 -> 60` en `nlloc_ring.d`.

Verificado: compila sin errores nuevos, el modo offline sigue refinando (rc=0, ARC de 3303 bytes),
y la cadena del guardia esta en el binario.

**Pendiente**: la **tasa de activacion** del guardia necesita una prueba CON ANILLO (el modo
offline no pasa por la cola). Aviso de metodo: mis dos intentos de medir "cuantas re-emisiones son
identicas" quedaron **no concluyentes** — agrupe mal dos veces (por tank, y por un campo que no es
el id de evento que usa la cola). No se saca conclusion de ahi; la justificacion del guardia es
estructural (evita el re-calculo perpetuo), no medida.

## 9. Techo real de `NumThreads`: el 42 % serial NO esta en `RefineHypo`

`RefineHypo` (`refine.c:117`) **si** es reentrante y limpio: solo lee `st/window/tt/cfg`, escribe
su propio candidato y hace mallocs locales, sin `logit` ni estado global. El corte para paralelizar
seria trivial. **Pero no sirve**: el bucle de candidatos (`csnloc.c:450`) recorre `nev`
candidatos con tope `MaxEventsPerWindow` (default 5, techo de array 16), y el fan-in
(`Upsert -> FormatHYP2000ARC -> emit_hypo`) **debe** quedar serial porque el `seq++` de la
numeracion (`csnloc.c:471`) y el first-match del registro (`event_registry.c:86-94`) dependen del
orden. Con `nthreads_eff = min(NumThreads, nev) <= 5` sobre una etapa parcial, el techo medido
(2.34x) no se mueve.

Donde si esta el tiempo serial: `AssembleCandidates` corre **una vez por grilla activa**
(`csnloc.c:432`, bucle `lev/gi`, reentrante con mallocs locales), `find_nucleations` va serial
tras el `join` (`backprojection.c:623`), y `Upsert/Format/emit` son irreductiblemente seriales.

**Siguiente paso**: instrumentar por fases (slices de retroproyeccion / `find_nucleations` /
`AssembleCandidates` por grilla / `RefineHypo` / `Upsert+Format+emit`) y medir el reparto real
antes de paralelizar. El corte candidato es el bucle por grilla, con acumulacion ordenada, en el
estilo de `backprojection.c:605-620`.

### 6.3 Prueba directa: el ARC de salida marca la S como NO USADA

El ARC de salida de hyp2000 tiene longitud fija 132 y **escribe los campos calculados** (peso
solucion `KSWT`, residual, etc.) segun la fase se haya usado o no (`hylst.for:875-895`, FORMAT
`1005`). Comparando la misma estacion:

- linea **P**: `len=120`, con campos calculados en `[114:120]` (p. ej. `'0     '`);
- linea **S**: `len=132` y `[114:132]` = **18 espacios en blanco** — sin ningun campo calculado.

O sea `hylst` emite la S como **fase no usada**, con los campos vacios, mientras la P de esa misma
estacion si los lleva. Es la confirmacion directa e independiente de `SWT 0.0 ≡ SWT 5.0 ≡ base`:
**hyp2000 no mete la S a la solucion**.

Por el codigo, la S **si se registra** (`hyloc.for:218` le asigna `WTVALS(LSWT+1)*WFAC*SWT`, con
`LSWT=0` → peso pleno) y **si entra** a las ecuaciones normales (`hyloc.for:420-425` pondera todas
las filas; `HYSOL` no excluye por `MSFLAG`). El unico sitio que puede llevar su peso a cero sin
tocar las P es la **ponderacion por residual** (`hyloc.for:384-393`: `RES=(|R|-RMSW1*TEMP2)/TEMP;
RES>1 → W=0`). Eso apunta a un problema de **dato**, no de codigo: tiempo S que no cuadra con el
reloj del evento, o Vp/Vs 1.75 muy lejos del real.

**Siguiente paso para cerrarlo**: habilitar el fichero de impresion de HYPOINVERSE (`KPRINT`/`PRT`)
para leer el `SRES` por fase, o comparar contra un Vp/Vs medido. No es observable en el ARC (la S
no usada no lleva residual).

**Impacto**: es un hueco real de capacidad. Sin S, hyp2000 no tiene buena restriccion de
profundidad/distancia — justo donde nlloc le saca ventaja (nlloc si usa la S via `p[14]` y
`LOCMETH`'s VpVs).

## 6.4 Estado del arreglo de `hypo_out.c`

El arreglo **queda aplicado y es correcto**: la linea de fase del ARC es ahora canonica
(`[14]='S'`, `[16]='4'`=LPWT, tiempo S en 41-45, `[46:48]='S '`=KSRK, `[49]='0'`=LSWT), verificado
byte a byte contra el ARC de entrada real. Su valor es de **forma** (formato correcto para todos
los consumidores) — **no** desbloquea las S en hyp2000, cuyo motivo es el de §6.3.


---

## 10. Causa raiz del descarte de las S: faltaban los horizontales en `estaciones_hyp.sta`

Cerrado el hilo de §6. El diagnostico se hizo habilitando el fichero de impresion de HYPOINVERSE
desde el propio `.hyp` (no hace falta recompilar): `PRT 'hypo.prt'` + `KPR 2` + `REP T T`
(`hycmd.for:198-247`). Ahi aparece, por cada S:

```
 *** SKIP PHASE CARD WITH UNKNOWN STATION:
 MT05 C1  HHN  S 420260918165232.00       32.00S  0    --
```

**Todas** las S se descartaban por "unknown station", y todas eran de canales **horizontales**
(`HHN`/`HHE`), que es donde `pickS` pica. La prueba esta en la misma corrida: `BO01 C1 HHN S`
rechazada y `BO01 C1 HHZ P` aceptada.

**Mecanismo**: HYPOINVERSE empareja la estacion por `STA + NET + COMP3 + LOC` (`hyphs.for`,
bloque del `GOTO 35`). `mk_sta.py` generaba el `.sta` con **solo el canal vertical**
(`estaciones_107.txt` trae `HHZ`), asi que `COMP3=HHN`/`HHE` no existia y la tarjeta se descartaba.
Por eso la S **entraba al ARC pero no a la solucion**, y por eso `POS`/`PSM` eran inertes: sin S no
hay nada que escalar.

**Fix** (`mk_sta.py`): por cada estacion se emiten **tres** tarjetas — el canal tal cual y sus dos
horizontales derivados (`HHZ -> HHN`, `HHE`), con dedupe. `estaciones_hyp.sta` pasa de 137 a 387
tarjetas (129 estaciones x 3).

**Verificado**: `SKIP PHASE CARD` 14 -> **0**; la linea de stats pasa de `NWR=7 NWS=0` a
`NWR=11 **NWS=11**`; las filas S del ARC de salida dejan de ir en blanco (len 132 -> 120, con
campos calculados).

### 10.1 Impacto medido (set de produccion `hb_d15_z20`, suelo de 2211 ARC)

| | <=25 km | <=50 km | <=100 km | mediana | abs(dz) | RMS |
|---|---:|---:|---:|---:|---:|---:|
| sin S | 302 | 355 | 372 | 11.2 | 13.4 | 0.673 |
| **con S (fix)** | **322** | **356** | **374** | **9.1** | **10.1** | 0.695 |

+20 eventos a <=25 km, mediana 11.2 -> 9.1 km, `abs(dz)` 13.4 -> 10.1 km. El RMS sube un poco
(0.673 -> 0.695), esperable al sumar fases S con residuales grandes — pero la geometria mejora
claramente.

### 10.2 `POS` (Vp/Vs) deja de ser inerte

Con la S dentro, `POS` mueve el 45.8 % de las geometrias (hasta 285 km epicentral, 114 km de
profundidad):

| variante | <=25 km | mediana | abs(dz) |
|---|---:|---:|---:|
| `POS 1.65` | 308 | 13.6 | 24.7 |
| `POS 1.85` | 329 | 10.3 | 13.4 |
| produccion (6 bandas, sin `POS`) | 322 | 9.1 | 10.1 |

Conclusion: **el eje Vp/Vs ahora es accionable y hay que barrerlo** — pero el modelo **por bandas
regionales** (que varia el ratio implicitamente) le gana en profundidad a cualquier `POS` unico.
Queda como pendiente del proximo barrido.

### 10.3 Trampa de metodo: donde vive el `.sta`

El arnes de refinadores (`calibrate_refiners.py:244`) copia `*.crh` y `estaciones_hyp.sta` desde
`tmp/hyp2000_ring/` **solo si no existen** en el WorkDir. Un WorkDir ya creado conserva el `.sta`
viejo y esconde el cambio: hay que usar un `--work` nuevo (o borrar el `wd/`). El modo anillo usa
el de `params/`.


---

## 11. Re-calibracion de `hyp2000` con la S ACTIVA

Toda la calibracion anterior (§2) era **S-ciega**: el `.sta` no tenia los horizontales y la S
nunca entraba (ver §10). Repetidos los barridos sobre el suelo de 2211 ARC con la S ya activa, el
punto de partida es el set de produccion (`hb_d15_z20`: 6 bandas + `DAM 15` + `ZTR 20`) =
**322/356/374, mediana 9.1, `abs(dz)` 10.1**.

### 11.1 Eje Vp/Vs (`POS`), modelo de produccion

| `POS` | <=25 km | <=50 km | <=100 km | mediana | abs(dz) | RMS |
|---|---:|---:|---:|---:|---:|---:|
| 1.60 | 283 | 346 | 373 | 14.9 | 21.8 | 0.796 |
| 1.65 | 306 | 351 | 374 | 13.1 | 19.9 | 0.740 |
| 1.70 | 317 | 352 | 374 | 10.7 | 13.7 | 0.704 |
| 1.73 | 322 | 356 | 374 | 9.4 | 11.9 | 0.699 |
| **1.75 (= produccion)** | **322** | 356 | 374 | **9.1** | **10.1** | 0.695 |
| 1.78 | 322 | 359 | 375 | 9.5 | 10.6 | 0.693 |
| 1.80 | 321 | 358 | 375 | 9.9 | 11.4 | 0.702 |
| 1.85 | 315 | 359 | 375 | 10.7 | 14.7 | 0.728 |
| 1.90 | 316 | 359 | 375 | 11.5 | 18.8 | 0.761 |

La profundidad dibuja una parabola con minimo en **1.73-1.78**; la produccion (1.75 implicito, el
default de `hybeg.f:57`) cae justo en el optimo. **No hay que cambiar nada** — pero ahora el eje
esta validado y es accionable si se quiere afinar por region.

Atribucion del valor de la S: `SWT 0` (S apagada) da 305/353/372, mediana 11.2, `abs(dz)` 13.9 —
o sea la S aporta **+17 eventos a <=25 km y -3.8 km de error de profundidad**.

### 11.2 Eje `RMS` (down-weighting por residual) y `SWT`

`RMS <ITRRES> <RMSCUT> <RMSW1> <RMSW2>` (`hycmd.for:402`; defaults `4 .16 1.5 3.`).

| set | <=25 | <=50 | <=100 | mediana | abs(dz) | RMS |
|---|---:|---:|---:|---:|---:|---:|
| **base (defaults)** | **322** | 356 | 374 | 9.1 | 10.1 | 0.695 |
| `ITRRES 8` | 319 | 354 | 373 | 9.1 | 11.2 | 0.706 |
| `ITRRES 12` | 319 | 355 | 374 | 9.1 | 10.7 | 0.709 |
| `ITRRES 100` (sin castigo) | 321 | 359 | 374 | **9.0** | **9.4** | 0.763 |
| `RMSW1 1.0` | 313 | 350 | 373 | 9.2 | 11.5 | **0.564** |
| `RMSW1 2.0` | 319 | 357 | 374 | **8.9** | 11.2 | 0.813 |
| `RMSW2 5.0` | 320 | 360 | 374 | 9.1 | 10.2 | 0.817 |
| `RMSCUT 0.10` | **322** | 355 | 373 | 9.1 | 10.1 | 0.696 |
| `RMSCUT 0.30` | 320 | 356 | 374 | 9.0 | 10.2 | 0.698 |
| `SWT 0.5` | 315 | 355 | 373 | 10.4 | 11.4 | 0.673 |
| `SWT 2.0` | 319 | 356 | 373 | 9.1 | 11.4 | 0.656 |

Conclusiones:
- **Ninguna variante domina a los defaults**: la produccion esta en el optimo tambien aqui.
- `SWT 1.0` (default) es el mejor; 0.5 y 2.0 empeoran.
- `ITRRES 100` es el unico que insinua ganancia (`abs(dz)` 9.4 y mediana 9.0) pero a costa de un
  RMS mucho peor (0.763) y un evento menos a <=25 — **compromiso a decidir con las tres familias**,
  no auto-aplicable.
- `RMSW1 1.0` baja el RMS a 0.564 pero empeora la geometria: confirma que el RMS **solo** no sirve
  como criterio de decision.


---

## 12. Reparto real por fases en `csnloc` (cierra el punto 8 del plan)

Instrumentacion anadida en `csnloc.c` (`process_window`): acumuladores `clock_gettime` por fase y
una linea `csnloc: fases ventanas=...` a `Debug >= 1` (solo tiempos, no altera el resultado; en
produccion `Debug 0` no imprime nada). Medido con el modo offline sobre 25 tanks / 684 picks
/ 50 ventanas / 108 hipocentros:

| fase | ms | % |
|---|---:|---:|
| `bproj` = `BackProject_Nucleations` (ya paralelizada) | 16769 | **50 %** |
| `asm` = `AssembleCandidates` (por grilla, **serial**) | 13151 | **39 %** |
| `reg` = `EventRegistry_Upsert` (renuclea dentro) | 2890 | 9 % |
| `refine` = `RefineHypo` del bucle de candidatos | 705 | **2 %** |
| `snap` / `dedup` / `fmt` | ~0 | ~0 % |

**Conclusiones**

1. **El punto 8 del plan estaba mal encaminado**: `RefineHypo` es solo el **2 %** del tiempo, asi
   que paralelizarlo (tope `nev <= MaxEventsPerWindow = 5`) no moveria nada. Confirmado con datos.
2. El serial que explica el techo de 2.34x esta en **`AssembleCandidates` (39 %)**, que corre una
   vez por grilla activa en el bucle `lev/gi` (`csnloc.c:417-437`).
3. Las grillas son independientes entre si y `AssembleCandidates` es reentrante (mallocs locales,
   sin estado global), asi que el corte limpio es **paralelizar el bucle por grilla** acumulando
   por offset precalculado (`events + nall`), preservando el orden de los indices para que la
   numeracion (`seq++`) y el first-match del registro no cambien.
4. Efecto esperado: el serial bajaria de ~50 % a ~11 %, o sea el techo de `NumThreads` pasaria de
   2.34x a ~9x. Es la mejora grande pendiente.


---

## 13. Verificacion LIVE del guardia `dirty` de la cola de NLLoc

Pendiente desde §8: hacia falta una prueba **con anillo** (el modo offline no pasa por la cola).
Montado con `tank_tools/tank_replay.sh play replay/tanks/chunk_000.tank --mode realtime` (en `fast`
el reloj de pared casi no avanza y el intervalo de 60 s no se cumple nunca, asi que el guardia no
llega a evaluarse) y `Debug 2` en `nlloc_ring.d` para ver las decisiones.

**Hallazgo de paso**: `startstop_replay.d` **no incluia los refinadores** (`hyp2000_ring` /
`nlloc_ring`), a diferencia de `startstop_unix.d`. Corregido: ahora el replay reproduce la cadena
completa, que es lo que se espera de el.

Resultado del log del refiner (3 eventos distintos del replay):

```
ev=1769600000 v=0001 ... NLLoc ok (6 fases) dt_ms=9063
ev=1769600001 v=0001 ... NLLoc ok (5 fases) dt_ms=2976
ev=1769600002 v=0001 ... NLLoc ok (4 fases) dt_ms=791
ev=1769600001 v=0002 ... NLLoc ok (6 fases) dt_ms=4347   <- re-emision con MAS fases
```

y **1870** lineas `sin cambios, se omite NLLoc`.

**Conclusion**: el guardia se comporta exactamente como se diseño —
1. refina la **primera** version de cada evento;
2. **omite** las re-emisiones cuyo contenido es identico (salvo el campo `version`): 1870 veces;
3. **vuelve a refinar** cuando el ARC cambia de verdad (`v=0002` con `nph` 5 -> 6).

O sea: la pregunta que habia quedado abierta en §8 ("cuantas re-emisiones son identicas") queda
respondida **empiricamente**: las del mismo evento con el mismo contenido **si** lo son, asi que el
guardia tiene valor real — sin el se re-ejecutaria NLLoc (~8 s por ARC) sobre trabajo ya hecho.

Nota de ruido: el log de omision esta dentro del barrido de `pend_process`, que gira en cada
iteracion del bucle, asi que el conteo (1870) **no** es "corridas evitadas" sino iteraciones; lo que
importa es que la omision se dispara cuando corresponde y no cuando el ARC cambia.


---

## 14. Paralelizacion del bucle de grillas de `csnloc`

Sobre §12 (`AssembleCandidates` = 39 % del tiempo, serial). Plan: paralelizar el bucle de grillas
(`csnloc.c`, niveles `GLOBAL`/`REGIONAL`/`LOCAL`), que es donde vive ese 39 %.

### 14.1 El arreglo previo que lo hace posible: filtrar por nivel en `Grid_IsActive`

La regla de activacion por cercania (`gridset.c`) decia "cercania a una nucleacion de un nivel
**mas grueso**", pero iteraba **todos** los candidatos acumulados, incluidos los del mismo nivel.
Eso hacia que las grillas de un nivel dependieran entre si (y del orden), imposibilitando una
paralelizacion exacta. Ahora se filtra por `nuc[i].grid_level >= g->level` — que es lo que el
comentario ya decia. Con eso, la activacion de todas las grillas de un nivel se resuelve de una vez
y el bucle se puede paralelizar **sin cambiar el resultado**.

### 14.2 Implementacion

- Cada grilla activa del nivel nuclea (`BackProject_Nucleations`) y ensambla
  (`AssembleCandidates`) en **su propio slot** de `events` (el buffer ya estaba dimensionado
  `grids.n x CSLOC_MAX_EVENTS`).
- Se compactan en **orden de grilla**, que es el mismo orden que producia la version serial, para
  no alterar el `seq++` de la numeracion ni el first-match del registro.
- El presupuesto de hilos se reparte: `nthreads_por_grilla = max(1, NumThreads / n_grillas_activas)`,
  de modo que el total no supere `NumThreads` ni se quede sin hilos la retroproyeccion.

### 14.3 Exactitud: verificada

**203/203 ARC identicos byte a byte** en 40 tanks frente al binario serial anterior (mismo `.d`,
misma captura). La particion por grilla no cambia ni una solucion.

### 14.4 Speedup: modesto, y por que

| version | tiempo (25 tanks) |
|---|---:|
| serial de grillas (`NumThreads 8`) | 71 s |
| 1 hilo por grilla | 67 s |
| reparto proporcional (aplicado) | **63 s** |

**~11 %.** El motivo es claro: la ganancia esta acotada por **cuantas grillas hay activas por
nivel**, y en la practica son 1-3 (1 global + 1 regional + 10 locales, de las que rara vez hay mas
de 2-3 en vuelo). Con `G` grillas el techo es `1/(0.61 + 0.39/G)` — con `G=2`, 1.24x; se mide 1.13x
por el reparto de hilos y el overhead.

**Conclusion honesta**: la paralelizacion es correcta, exacta y se queda (no estorba), pero **no es
la mejora grande** que se esperaba. Para bajar el serial de verdad habria que cortar **dentro** de
`AssembleCandidates` (la asignacion de fases y el RMS/gap por candidato, o el DBSCAN), que es un
cambio bastante mayor. El otro 50 % (`bproj`) ya estaba paralelizado y su techo con `NumThreads` es
el medido en §9.


---

## 15. Perfilado fino de `AssembleCandidates` y paralelizacion del DBSCAN

### 15.1 Donde estaba el 39 %: dentro del DBSCAN

Instrumentacion interna (contadores en `backprojection.c`, expuestos con
`AssembleCandidates_Stats`): sobre 50 ventanas,

```
asm  dbscan=15354ms  prom+rep=33ms  reclamo+stats=16ms  predict_tt=25597 llamadas
```

**`DBSCAN_Cluster` es el 99.7 %** de `AssembleCandidates`. El reclamo codicioso (secuencial por
naturaleza: asigna picks de forma exclusiva) y las estadisticas por hipotesis son **16 ms**. La
redundancia que se sospechaba (`predict_tt` se llama dos veces por par hipotesis/pick, en el
reclamo y en las estadisticas) existe — 25597 llamadas — pero es irrelevante en costo.

Conclusion de metodo: **el punto a optimizar no era el reclamo**, era el DBSCAN. Se llego ahi
midiendo, no suponiendo.

### 15.2 Intento descartado: matriz de adyacencia

Primero se probo calcular los n^2 pares **una sola vez** en una matriz y que la expansion la
consultara (antes se recalculaban). **Salio mas lento** (17799 ms vs 15354): las nucleaciones por
grilla llegan hasta `CSLOC_MAX_NUC` = 4096, asi que n^2 = 16 M — la matriz excede el tope de 4 MB y
no se construye, y en los casos chicos crear 8 hilos cuesta mas que el trabajo. Descartado.

### 15.3 Aplicado: conteo de nucleos en paralelo

El unico tramo **embarazosamente paralelo** del DBSCAN es el conteo de vecinos por punto: cada fila
escribe solo su `is_core[i]`. Se reparte por filas entre `NumThreads` hilos, **sin memoria extra**
(no hay matriz). La expansion sigue igual (secuencial, es un flood-fill).

- **Equivalencia**: hipocentros **identicos** (108/108 en la corrida offline; mismo input).
- **DBSCAN**: 15354 -> **11418 ms** (1.34x).
- **Tiempo de pared** (25 tanks): 71 s (serial) -> 63 s (grillas en paralelo) -> **60 s** (con el
  DBSCAN). **~15 % acumulado**, con resultado identico.

### 15.4 Lo que queda, y es algoritmo, no hilos

El DBSCAN es un **O(n^2) con n grande** (hasta 4096 nucleaciones). Ninguna cantidad de hilos cambia
eso: el techo de paralelizacion del conteo es `n^2/NumThreads` y la expansion sigue siendo `n^2`.
La mejora real seria un **indice espacial** — las nucleaciones viven en una grilla regular, asi que
un bucketing uniforme de lado `eps` (50 km) bajaria la busqueda de vecinos de `O(n^2)` a ~`O(n)`, y
es igualmente determinista si se preservan los conjuntos de vecinos. Es un cambio de clase de
complejidad, bastante mayor, y queda anotado como pendiente.

Nota de contabilidad: con grillas en paralelo, los acumuladores de fase suman los tiempos de cada
grilla, asi que `bproj`/`asm` quedan **inflados** respecto del tiempo de pared. La metrica limpia es
el tiempo de pared; los porcentajes del reporte de fases deben leerse como costo, no como pared.


---

## 16. Evaluacion medida: un indice espacial en el DBSCAN vale la pena

### 16.1 Primero, una correccion de metodo

Los acumuladores de la instrumentacion de §15 eran `static` y los escribian **varios hilos** (las
grillas de un nivel corren en paralelo), asi que habia **carrera** y los numeros no eran fiables:
el contador interno del DBSCAN (15354 ms) salia **mayor** que la fase que lo contiene (13151 ms),
lo cual es imposible. Se corrigieron con mutex (y contadores atomicos para `predict_tt`). Los
numeros de esta seccion ya son de la version sin carrera.

### 16.2 La medicion que decide

Histograma de `n` (nucleaciones por llamada) y pares evaluados, sobre 50 ventanas / 108
hipocentros:

```
llamadas=255   sum_n=615787   sum_n2=2 253 951 361
pares_en_eps=120 105 819      max_n=4096      ratio_n2_near=18.8
hist = [1, 4, 5, 2, 7, 4, 10, 13, 27, 182]     (n en [2^k, 2^k+1))
```

Lectura:

- **`n` promedio = 615787/255 = 2415**, y `max_n = 4096` (toca el tope `CSLOC_MAX_NUC`). El DBSCAN
  trabaja con **n grande**, no con decenas: **182 de 255 llamadas tienen n >= 512**.
- De **2 254 millones** de pares evaluados, solo **120 millones (5.3 %)** estan dentro de `eps`.
- **`ratio_n2_near = 18.8`**: hay ~19x de trabajo que un indice espacial se ahorraria.

### 16.3 Veredicto

**Vale la pena.** El DBSCAN es ~23 % del tiempo total (ya con el conteo de nucleos paralelo) y su
costo es `O(n^2)` con n de miles. Un indice espacial lo bajaria ~12-19x, o sea **~20 % del tiempo
total**, encima del 15 % ya ganado. Y es el ultimo bloque grande: el otro 72 % (`bproj`) ya esta
paralelizado y su techo es el de §9.

### 16.4 Como mantenerlo determinista (condiciones no negociables)

1. **El indice solo descarta candidatos**; la comparacion final tiene que seguir siendo el mismo
   `sq_dist(...) <= eps2`, bit a bit. Si el indice calculara distancias por su cuenta (otro orden de
   suma, otra celda) los casos de borde `dist == eps` podrian caer de otro lado y cambiar los
   vecinos.
2. **El recorrido de vecinos debe seguir siendo ascendente.** El DBSCAN asigna los puntos de borde
   al primer nucleo que los alcanza, asi que el orden de iteracion influye en las etiquetas. Con un
   indice hay que recolectar los candidatos y ordenarlos por indice antes de recorrerlos.

Verificacion: hipocentro a hipocentro (y ARC a ARC) sobre la captura completa, como en §14 y §15.

### 16.5 Detalle util para implementarlo

Las nucleaciones **viven en una grilla regular** (lat/lon/profundidad con `NodeKm` de 100/25/10 km),
asi que el indice no necesita ser generico: un bucketing 3D con celdas de lado `eps` permite
localizar las celdas vecinas por aritmetica directa. La cuarta dimension del DBSCAN (`dt0*8 km/s`)
tiene dispersion amplia, asi que conviene bucketear por las 3 espaciales y filtrar `t0` con el
predicado exacto.


---

## 17. Indice espacial en el DBSCAN: implementado y verificado

### 17.1 Diseno

Celdas de lado `eps` en el espacio de features (4 dimensiones) con encadenamiento por arrays
(`head[]` + `next[]`), y para cada punto se recorren las 3^4 = 81 celdas vecinas.

**Exactitud por construccion**: un vecino con `|dx| <= eps` en cada eje no puede estar a mas de una
celda de distancia, asi que las 81 celdas contienen **todos** los vecinos. Y el indice **solo
descarta**: la comparacion final sigue siendo el mismo `sq_dist(...) <= eps2`, de modo que los
conjuntos de vecinos son identicos a los del barrido completo.

**Orden de recorrido**: el DBSCAN asigna los puntos de borde al primer nucleo que los alcanza, asi
que el orden influye en las etiquetas. El indice devuelve otro orden, y por eso se ordena — pero
**solo cuando `min_pts > 1`**. Con `min_pts = 1` (el caso de produccion) todos los puntos son
nucleo, los grupos son las componentes conexas y el orden no influye; ordenar por cada punto se
comeria la ganancia (con n de miles, ~9500 comparaciones por punto contra ~200 utiles).

### 17.2 Resultado

| version | DBSCAN | 25 tanks (pared) |
|---|---:|---:|
| barrido completo | 13031 ms | 60 s |
| con indice espacial | **6530 ms** | **54 s** |

**Hipocentros identicos** (108/108 en la corrida offline, mismo input). La progresion acumulada de
toda la optimizacion: **71 s (serial) -> 63 s (grillas) -> 60 s (nucleos DBSCAN) -> 54 s (indice)**,
o sea **~24 %**, sin cambiar un solo resultado.

### 17.3 Por que 2x y no los ~19x que sugeria `ratio_n2_near`

`ratio_n2_near = 18.8` acota el trabajo **de distancias** que se ahorra, y eso se cumple (las
distancias evaluadas bajan ~12x). Pero el costo pasa a estar en **el recorrido de las 81 celdas**:
615k consultas x 81 celdas = ~50 M busquedas en la tabla de dispersion, cada una con su
computo de hash y su cadena (la tabla tiene 65536 entradas para 615k puntos, o sea cadenas de
~10). Ese recorrido cuesta del orden de lo que queda del barrido, y de ahi el 2x.

**Negativo util**: bucketizar tambien la cuarta dimension (`dt0*8`) **no aporta** (6530 vs 6622 ms):
los puntos que comparten celda espacial ya comparten el tiempo, asi que los candidatos de las 27
celdas espaciales ya eran casi exactamente los vecinos reales.

**Refinamiento pendiente** (si algun dia hace falta): reemplazar la tabla de dispersion por un
**arreglo indexado directamente** por celda. Las celdas espaciales viven en un rango acotado
(lat/lon en km + profundidad), asi que un arreglo plano de ~10^6 enteros por llamada daria acceso
O(1) sin colisiones, y el recorrido de las 27 celdas dejaria de ser el cuello. Es la unica via para
acercarse al 12x de las distancias. Hoy no se justifica: el DBSCAN ya es ~13 % del tiempo total y el
72 % restante (`bproj`) esta paralelizado con su techo medido en §9.
