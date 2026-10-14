# Overlays de líneas (`.lines`) para `csnrv` / `csnstaevdisp`

Capa vectorial de líneas (subducción, fallas activas, límites de regiones…)
que se dibuja **sobre el mapa base** y **debajo de estaciones/sismos**. Se
declara en el `.d` y se colorea desde el archivo o desde la GUI.

Implementación: `ew_gui_tools/common/{include/ewgui/overlay.h, src/ewgui_overlay.c}`
(parser + dibujo cairo, sin GTK) y `src/ewgui_overlay_ui_gtk4.c` (diálogo).

## Formato `.lines` (texto plano)

```
# comentario
#layer name=<N> [color=<RRGGBB>] [width=<F>]
<lon> <lat>
<lon> <lat>
>
<lon> <lat>
#layer name=<N2> ...
```

- `#layer` abre una capa; `name` es obligatorio (si falta, se autogenera
  `layerN` y se avisa). `color` = `RRGGBB` con o sin `#` (defecto negro);
  `width` = píxeles de pantalla (defecto 1.5).
- Cada línea de datos es un vértice `lon lat`.
- `>` cierra la polilínea actual; el siguiente vértice abre otra.
- Líneas vacías y cualquier `#` que no sea `#layer` se ignoran.
- Errores de formato (coordenada inválida, `#layer` sin nombre…) se avisan
  por `stderr` y se saltan; no abortan la carga.

## Claves del `.d` (todas opcionales)

```
OverlayFile   ../../resources/overlays/placas.lines       # repetible (máx. 16)
OverlayFile   ../../resources/overlays/subduccion.lines
OverlayFile   ../../resources/overlays/regiones.lines
OverlayColor  regiones 5A5A5A                             # <capa> <RRGGBB>; repetible
OverlayWidth  1.2                                         # ancho global (px)
OverlayHide   fallas                                      # <capa> que no se dibuja
```

- La ruta es relativa al cwd del módulo (= `run_working_v8/params/`), igual que
  `MapImageFile`; de ahí `../../resources/overlays/...`.
- Precedencia de color: `OverlayColor` (`.d`) > `color=` (archivo) > negro.
  Ancho: `OverlayWidth` (`.d`) > `width=` (archivo) > 1.5.
- `OverlayHide` (repetible) apaga capas por nombre sin quitarlas de la lista: el
  archivo se sigue cargando y el resto de la capa se controla con
  `OverlayColor`/`OverlayWidth`. Una capa que no exista se avisa por stderr (o
  al log) y no aborta.
- Sin `OverlayFile` el módulo se comporta como antes (sin capa).
- Los tres módulos con overlays (`csnrv`, `csnstaevdisp`, `csnhypodbp`) leen las
  mismas claves; `--check-overlays` (los dos primeros) y `--print-config`
  (`csnhypodbp`) reportan por capa `visible`, `color`, `width` y `polylines`.

## GUI

Menú **Overlay** del headerbar:
- *Layers…*: ventana modal con una fila por capa (nombre, visibilidad, color).
- *Reload*: relee los `OverlayFile` sin reiniciar.

Los cambios del diálogo son **en memoria**; el módulo loguea la línea
`#layer name=… color=… width=…  (copy to overlay file)` para copiarla al
archivo si se quiere conservar.

## Datos incluidos y cómo regenerarlos

`resources/overlays/` (no versionado; se generan con `make_overlays.py`):

| archivo | contenido |
|---|---|
| `subduccion.lines` | zanja de subducción PB2002 (solo `Type=subduction`, Nazca/Sudamérica) en el margen chileno |
| `regiones.lines` | contorno de cada una de las 16 regiones de Chile (Natural Earth admin-1 **polígonos**; cuerpo + islas grandes) |
| `fallas.lines` | catálogo CHAF (963 trazas) |
| `placas.lines` | márgenes de placas de **todo el mundo** (PB2002, 241 segmentos): capa `placas` + capa `placas_sub` (solo zonas de subducción) |
| `PROVENANCE.txt` | fuentes, fecha y licencias |

Regenerar (solo stdlib; no necesita GDAL):

```
python3 ew_gui_tools/common/tools/make_overlays.py all
python3 ew_gui_tools/common/tools/make_overlays.py check   # valida los .lines
python3 ew_gui_tools/common/tools/make_overlays.py check-lines <archivo>...  # uno suelto
```

Descarga las fuentes a `/tmp/opencode/overlay_src` (configurable con `--src`)
y escribe en `resources/overlays` (configurable con `--out`).

### Fuentes y licencias
- **PB2002** — Bird, P. (2003), *Geochem. Geophys. Geosyst.* 4(3), 1027;
  vía `github.com/fraxen/tectonicplates` (ODC-By 1.0; crédito a Hugo Ahlenius /
  Nordpil y a Peter Bird).
- **Natural Earth** 10m admin-1 (polígonos) — dominio público (S. Pact).
- **CHAF** — Melnick, Maldonado & Contreras (2020), PANGAEA
  [10.1594/PANGAEA.922241](https://doi.org/10.1594/PANGAEA.922241), CC-BY-4.0.
