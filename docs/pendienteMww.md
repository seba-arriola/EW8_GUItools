# Pendiente: `Mww` como magnitud de primera clase (y su integración en los módulos)

Estado a la fecha de este documento: la inversión de fase W **ya calcula** el `Mww` y la GUI
lo **guarda en la base** al confirmar la solución, pero `Mww` **no viaja por el ARC** ni lo lee
ningún otro módulo. Esto es lo que queda por hacer, en orden de dependencia.

## 1. Por qué falta: el ARC sólo admite cuatro tipos

La marca de magnitud preferida de una solución es **un solo carácter** en el ARC, en el campo
`BMTYPI` (línea 1, columna 123 = offset 122; ver `common/include/ewgui/arc_layout.h`). El
contrato es el de HYPOINVERSE y sólo tiene:

| carácter | magnitud |
|---|---|
| `L` | ML |
| `W` | Mwp |
| `B` | Mb |
| `S` | Ms |

Ese carácter viaja en el 13.er token de `TYPE_PICK_SCNL` (`csnhypodbp` → `csnloc` lo copia al
`BMTYPI` del ARC → `hypodb_ingest` lo traduce a `event.preferred_magnitude_id`).

`Mww` (la magnitud de momento de la inversión de fase W) **no cabe**: `W` ya es `Mwp`. Por eso
`csnhypodbp` publica las picadas del confirm con `pref = '-'` (sin tipo preferido) y la
preferencia del `Mww` se marca **sólo en la base**, con `ewguidb_set_preferred`, al confirmar.

Consecuencia a tener presente: si después llega otro ARC de ese evento (de `csnloc`,
`hyp2000_ring` o `nlloc_ring`) con su propio `BMTYPI`, `hypodb_ingest:update_preferred` vuelve a
recalcular la preferida y **puede pisar el `Mww`**.

## 2. Opción A (recomendada): ampliar el contrato de `BMTYPI`

Añadir un carácter para `Mww` (p. ej. `M`) al contrato del ARC y propagarlo:

1. `csnhypodbp` (el radio `Mww` del preview): `pref_code(4)` en `csnhypodbp_ws.c` devuelve hoy
   `'-'`; pasaría a devolver el carácter nuevo.
2. `csnloc`: `pick_scln.c` ya es genérico (copia el token tal cual) y `hypo_out.c` ya escribe
   `pk->pref_mag` en `EWGUI_ARC_BMTYPI_OFF`; hay que **documentar** el valor nuevo en
   `arc_layout.h` y ampliar `csnloc/test/test_pick_origin.c`.
3. `hypodb_ingest.c` (`update_preferred`): añadir `case 'M' → "Mww"` al `switch` del BMTYPI.
4. `common/tests/test_arc_layout.sh`: invariante del offset (no cambia) y del juego de
   caracteres (sí cambia).

**Riesgo a verificar antes**: ese byte lo lee también HYPOINVERSE al inicio del ARC
(`hyphs.for`, `FORMAT 1212 = T123,2(A1,F3.2,F3.1)`). Un `A1` admite cualquier letra, pero hay
que comprobar con el motor real que no altera la elección de magnitud de LOC (es la misma
familia del bug de la línea 1 que ya costó una regresión: ver AGENTS.md §7, "La línea-1 del ARC
no admite texto en 122..135").

## 3. Opción B (sin tocar el ARC): `Mww` como columna de la base

Dejar el ARC como está y que el `Mww` viva sólo en la base, pero hacer que **el resto lo lea**:

- `csnhypodbp`:
  - la tabla *Origins* (`csnhypodbp_ws.c`, `csnhypodbp_gui.c`) muestra `ML/Mwp/Mb/Ms` desde
    `nmag0..3` (columnas `c9..c12`). `Mww` no tiene slot: habría que añadir un 5.º (`c13` +
    `nmag4`) **o** leerlo de la BD para la fila seleccionada. Ojo: `CSNHYPODBP_MAG_SLOTS` y
    `CSNHYPODBP_MAG_NKIND` valen 4 y hay tests que lo fijan
    (`test_preview.c`, `test_ui_invariants.sh`).
  - la tablita de la solución (`csnhypodbp_preview` / `csnhypodbp_magcfg`) ya tiene la columna
    `Mww` para el valor de la inversión en curso; falta que la muestre también **al releer del
    historial** (`ewguidb_query_magnitudes`), no sólo tras un `Calculate` en la sesión.
- `csnrv`: la tabla `Preferred/Ml/Mwp` — añadir `Mww` y/o pintarlo en el mapa.
- `csnmags`: hoy calcula `ML/Mwp/Mb/Ms` (`MT_*`); `Mww` es de otro motor
  (`csnwphase`), así que no debería calcularlo. Lo que sí habría que decidir es si
  `pref_mag_type()` (`csnmags.c:596`) debe **respetar** un `Mww` ya calculado en lugar de
  proponer `Mwp`.
- `hypodb`: `magnitude.calculation_method = WPHASE` ya distingue el `Mww` de la fase W; si se
  quiere que la preferida del evento lo sea por defecto, el orden fijo de `update_preferred`
  (`Mwp > Ms > Mb > ML`, `hypodb_ingest.c:493`) tendría que incluir `Mww`.

## 4. Lo que ya está hecho (no hay que rehacerlo)

- `csnwphase` calcula `Mww` (convergencia, tensor, planos nodales) y lo deja en
  `wphase.json` (`csnwphase_parse.c`).
- La GUI construye el tensor y la magnitud y los escribe al **confirmar** la solución
  (`csnhypodbp_mww_persist`, `csnhypodbp_mww.c`), creando el `event`/`origin` si faltan.
- Convención de ids compartida entre la GUI y el ingester: `common/include/ewgui/db_ids.h`
  (`<origin_id>/mag/Mww`, `<origin_id>/tensor/WPHASE`).
- El esquema ya admite el tipo (`db/schema.sql:83`: *Magnitude scale: Mw, ML, Mb, Ms, Mww, etc.*).
