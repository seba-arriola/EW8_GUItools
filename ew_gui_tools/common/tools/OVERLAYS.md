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
OverlayFile   ../../resources/overlays/subduccion.lines   # repetible (máx. 16)
OverlayFile   ../../resources/overlays/regiones.lines
OverlayColor  regiones 5A5A5A                             # <capa> <RRGGBB>; repetible
OverlayWidth  1.2                                         # ancho global (px)
```

- La ruta es relativa al cwd del módulo (= `run_working_v8/params/`), igual que
  `MapImageFile`; de ahí `../../resources/overlays/...`.
- Precedencia de color: `OverlayColor` (`.d`) > `color=` (archivo) > negro.
  Ancho: `OverlayWidth` (`.d`) > `width=` (archivo) > 1.5.
- Sin `OverlayFile` el módulo se comporta como antes (sin capa).

## GUI

Menú **Overlay** del headerbar:
- *Layers…*: ventana modal con una fila por capa (nombre, visibilidad, color).
- *Reload*: relee los `OverlayFile` sin reiniciar.

Los cambios del diálogo son **en memoria**; el módulo loguea la línea
`#layer name=… color=… width=…  (copy to overlay file)` para copiarla al
archivo si se quiere conservar.

## Datos incluidos y cómo regenerarlos

`resources/overlays/` (no versionado; lo copia `deploy_portable.sh`):

| archivo | contenido |
|---|---|
| `subduccion.lines` | zanja de subducción PB2002 (solo `Type=subduction`, Nazca/Sudamérica) en el margen chileno |
| `regiones.lines` | contorno de cada una de las 16 regiones de Chile (Natural Earth admin-1 **polígonos**; cuerpo + islas grandes) |
| `fallas.lines` | catálogo CHAF (959 trazas) |
| `PROVENANCE.txt` | fuentes, fecha y licencias |

Regenerar (solo stdlib; no necesita GDAL):

```
python3 ew_gui_tools/common/tools/make_overlays.py all
python3 ew_gui_tools/common/tools/make_overlays.py check   # valida los .lines
```

Descarga las fuentes a `/tmp/opencode/overlay_src` (configurable con `--src`)
y escribe en `resources/overlays` (configurable con `--out`).

### Fuentes y licencias
- **PB2002** — Bird, P. (2003), *Geochem. Geophys. Geosyst.* 4(3), 1027;
  vía `github.com/fraxen/tectonicplates`.
- **Natural Earth** 10m admin-1 (polígonos) — dominio público (S. Pact).
- **CHAF** — Melnick, Maldonado & Contreras (2020), PANGAEA
  [10.1594/PANGAEA.922241](https://doi.org/10.1594/PANGAEA.922241), CC-BY-4.0.
