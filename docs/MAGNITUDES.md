# Magnitudes en `csnhypodbp` (sub-pestaña `magnitudes`)

La sub-pestaña `magnitudes` (dentro de la pestaña raíz `Procesar`, junto a
`picking`) sirve para **mirar la traza con el procesado que mide cada magnitud**,
**ajustar la ventana** de cálculo por estación y (pendiente: bloque G5) **forzar
el recálculo** y publicar el resultado como magnitud manual.

Regla de oro: **la física vive en `csnmags`**, no en la GUI. La GUI dibuja y
marca; lo que se dibuja tiene que ser lo que `csnmags` mide. Los valores por
estación salen del anillo `TYPE_MAGNITUDE` (o de la BD) y la magnitud de red del
`nmag<slot>` de la fila. La GUI **nunca** calcula una magnitud.

---

## 1. Qué mide cada magnitud y con qué ventana

Ventanas y canales tal como están en `ew_gui_tools/csnmags/csnmags.c`; la GUI lee
las mismas claves del **mismo `.d`** (`MagConfigFile` en `csnhypodbp.d`, por
defecto `csnmags.d`) con `csnhypodbp_magcfg_load()`.

| Magnitud | Qué mide | Canal | Ventana (offsets desde la P) | Claves |
|---|---|---|---|---|
| **ML** | Máx. amplitud simulada Wood-Anderson | horizontales `HHE/BHE/HNE` (vertical solo con `Ml_AllowVerticals`) | `Ml_TA` → `Ml_TB` (10 → 45 s) | `Ml_TA`, `Ml_TB`, `WoodAndersonCoefs` |
| **Mwp** | Integral del desplazamiento de la P (Tsuboi) | vertical `HHZ/BHZ/HNZ` | `Mwp_StartOff` → `+Mwp_T0` (0 → 95 s) | `Mwp_StartOff`, `Mwp_T0` |
| **Mb** | `A/T` de la P, banda 0.5–2 Hz, T≈1 s | vertical | P−1 → `Mb_Window` (30 s) | `Mb_Window`, `Mb_Band` |
| **Ms** | Rayleigh vertical ~20 s | vertical | P → `Ms_Window` | `Ms_Band` (0.045–0.056 Hz) |

`csnhypodbp_magcfg_window()` devuelve esos offsets en segundos respecto del
**ancla** de la estación; `csnhypodbp_magcfg_gate_text()` describe la puerta de
distancia (abajo).

### Vista de la traza (solo para juzgar la ventana)

`csnhypodbp_magview_build()` recorta la traza al rango dibujado y aplica la cadena
de la magnitud **en cuentas** (sin corregir por respuesta instrumental: eso lo
hace `csnmags`):

- **ML**: Wood-Anderson (`ewgui_filter_wood_anderson`).
- **Mwp**: `ewgui_integrate` (desplazamiento) + diezmado a ~10 sps.
- **Mb**: paso banda `Mb_Band`.
- **Ms**: **diezmado a ~1 sps** y luego paso banda `Ms_Band`. A 100 sps ese paso
  banda de 0.05 Hz es numéricamente inútil (queda por debajo de la resolución del
  filtro), de ahí el diezmado.

El dibujo usa **las mismas dos ramas que `picking`** (`csnhypodbp_view_use_polyline`
+ envolvente por tramos con `csnhypodbp_view_min_half`/`csnhypodbp_view_band_runs`)
para que las vistas diezmadas no salgan a guiones.

---

## 2. Puertas de distancia: qué estaciones entran

`csnmags` **ya descarta** las estaciones fuera de rango (`csnmags.c`, por
magnitud). La GUI lee las mismas claves y **no lista** las que el motor va a
tirar; la línea de estado dice cuántas quedaron fuera y con qué rango.

| Magnitud | Puerta por defecto | Estándar (fuente) |
|---|---|---|
| **ML** | `0–8°` **y** `≤600 km` epicentral (`Ml_DistType 1`) | IASPEI: R<1000 km (~9°); SeisComP 8° estricto; USGS 600 km (150 km en CEUS) |
| **Mwp** | `3–90°` | Tsuboi/SeisComP 5–105°; USGS "all"; sesgo: ≲30° sobrestima, ≳90° subestima |
| **Mb** | `5–105°` | IASPEI 20–100°; **SeisComP default 5–105°** (nota explícita); USGS 15–100° |
| **Ms** | `20–160°` (+ veta `h > 60 km` si `Ms_DeepCorr 0`) | IASPEI Ms_20 20–160°; Ms_BB 2–160°; SeisComP 20–160° y h≤100 km |

Claves en el `.d` de `csnmags`: `Ml_MinDelta`, `Ml_MaxDelta`, `Ml_MaxDist`,
`Ml_DistType`, `Mwp_MinDelta/MaxDelta`, `Mb_MinDelta/MaxDelta`,
`Ms_MinDelta/MaxDelta`.

**Consecuencia importante**: con una red regional (≤500 km ≈ 4.5°) **Ms no tiene
ni una estación elegible** y Mb tampoco (mínimo 5°). No es un fallo: es la puerta
del estándar. Para verlas hay que **bajar la puerta a mano** con el override de la
barra inferior (`distancia` → `min`/`max`), que se aplica solo a la magnitud
activa y viaja al forzado. La casilla de uso por estación permite además forzar
una estación concreta.

---

## 3. Barra inferior de `magnitudes`

- **`picadas: A M Teo`** — qué **picadas P** entran al forzado, por su **origen**:
  - **M** = picada manual,
  - **A** = picada automática,
  - **Teo** = P teórica (la estación no tiene P observada).
  Por defecto **A y M encendidos, Teo apagado**. Ajustar la franja de una
  magnitud **no** cambia la categoría: una picada automática con ventana tocada
  sigue entrando con `A`; la ventana tocada solo cambia los offsets de esa
  magnitud. Son el mismo concepto que los checkboxes A/M/Teo de `picking` (allí
  filtran qué picadas van al localizador).
  Ojo: el `locate` **no** usa estos checks del panel. Usa las casillas por fila y
  los A/M/Teo de `picking` (una P efectiva `M>A>Teo` por estación), calcula las
  cuatro magnitudes (ML/Mwp/Mb/Ms; Mww no se recalcula) y ancla cada ventana en el
  `t_pick` de esa P, usando la franja ajustada de este panel si existe. Las S no
  generan magnitud: solo van al localizador. La ventana es por `(estación,
  magnitud)` y sobrevive al cambio de estación; el **gesto** de arrastre no:
  cambiar de estación, magnitud o solución lo cancela (así un release perdido no
  deja el encuadre congelado ni bloquea la franja de la estación siguiente).
- **`distancia min/max`** — override manual de la puerta de la magnitud activa,
  en grados. El corte en km de ML (`Ml_MaxDist`/`Ml_DistType`) **no** se edita
  aquí: no es la misma distancia y se toma siempre del `.d` de `csnmags`.
  Al activarlo se siembra con la puerta vigente, así activar no cambia nada hasta
  que se edite un campo. Las cajas son **`GtkEntry`**, no `GtkSpinButton`: los
  botones `+/-` no aportaban y su marco se llevaba el ancho de la barra. El
  texto del campo solo se convierte en valor al **confirmarlo** (Enter) o al
  **salir del campo**; mientras no sea un número válido la caja se marca en rojo
  y la puerta vigente no cambia (al salir con el texto inválido se restaura el
  valor que sí se está aplicando). El volcado es **todo o nada**: con un campo
  inválido no se aplica ninguno. El rango (0–180°; profundidad 0–700 km el
  mínimo y 0–999 km el máximo, donde 999 = sin corte), los decimales (1/0) y el
  ancho de cada caja salen de
  `csnhypodbp_mag_ov_field_limits`/`_width_chars`/`_fmt`/`_parse`
  (`csnhypodbp_magcfg.{h,c}`), la misma fuente que el parseo, y se acepta `.` o `,`
  como separador decimal. El ancho de la caja es el del valor canónico del campo
  (5/5/3/3 caracteres) más `CSNHYPODBP_MAG_OV_WIDTH_MARGIN` (2), y se fija como
  `width-chars` **y** `max-width-chars` (≈45 px, en vez de los 150 px de ancho
  natural que GTK4 da a todo `GtkEntry` cuando no se acota). Los rótulos de los grupos
  y el texto de las cajas van al **72 %** del tamaño del tema (clases CSS
  `.mag-bar-cell`/`.mag-bar-label`; antes al 60 % y el usuario los vio chicos).
  `reset default`, `calculate` y `calculate all` van al **90 %** (clase
  `.mag-bar-btn`); los checkboxes `A/M/Teo` **no** cambian de tamaño.

## 4. Tabla de estaciones (panel izquierdo)

Una fila por **estación** dentro de la puerta de distancia, columnas
`station`(con casilla de uso) / `dist[km]` / `ancla`(M|A|Teo|-) / `t0[s]` /
`t1[s]` / `mag` / `res` / `stat`. La ventana vigente es la ajustada si existe; si
no, la default del `.d`. La casilla queda **inerte** si la estación no tiene traza
descargada.

## 5. Tablita de la solución (arriba, compartida con `picking`)

`Lat / Lon / Depth[km] / Nph / RMS[s] / ML / Mwp / Mb / Ms`. Vive **por encima del
sub-notebook**, así que no se pierde al cambiar entre `picking` y `magnitudes`.
Fondo rojo leve cuando muestra la **candidata** (del `locate` o, pendiente: del
forzado de magnitud) y `Confirm`/`Discard` sensibles solo con candidata: **nada se
hace preferido mientras el usuario no confirme**.

## 6. Petición de ondas

`MagFetchSeconds` (default **700 s**, tope 870 = buffer de 15 min) en
`csnhypodbp.d`: la petición por solución es larga a propósito para que Ms tenga su
ventana descargada. La traza ya se reserva a 15 min, así que no cambia la memoria.

---

## 7. Pendiente (bloque G5)

- `csnmags`: archivo de **overrides** de ventana por (estación, magnitud) +
  `Ms_Window` como clave (hoy está fija en 400 s) + lista de estaciones usadas en
  el JSON.
- Botones `Calcular la observada` / `Calcular todas` y publicación como magnitud
  **manual** (`algorithm = "CSNMan"`, `error` = residual), preferida sobre la
  automática al consultar la BD.
