# ADR-0001: backend de render del canvas en GTK4

- Estado: aceptado (Fase 5, T5.3)
- Fecha: 2026-10-07
- Contexto: migración de los visores de GTK3+cairo a GTK4+libadwaita.

## Contexto

En GTK3 los visores dibujan en un `GtkDrawingArea` con el signal `draw` y cairo.
En GTK4 el signal `draw` desaparece (`GtkWidgetClass.snapshot`) y dibujar con
cairo **por frame** en un `GtkDrawingArea` es **más lento que en GTK3** (se
resube la textura CPU a GPU cada frame). El canvas está aislado tras
`EwGuiCanvas` (`ewgui/view.h`), así que el backend se puede cambiar sin tocar
los módulos.

## Decisión

El backend por defecto en GTK4 será **`GtkDrawingArea` + `set_draw_func` con
dibujo cacheado en nodos GSK**:

- Construir `GskPath`/`GskPathBuilder` por traza y `gtk_snapshot_append_stroke()`.
- Cachear las capas estáticas (fondo, grilla, eje) como `GskRenderNode` y
  reconstruir solo lo que cambia.
- Un `GskPath` por traza; **nunca** un nodo por segmento.
- Conservar el *dirty flag* y la caché de envolvente (`ewgui_wave`).

Se pasará a **`GtkGLArea`** (OpenGL propio) **solo si la medición lo exige**:
si el bench de envolvente (C6) y el perfil con sysprof muestran que el coste de
`snapshot` por frame es el cuello de botella y no cabe en el presupuesto.

## Consecuencias

- La migración a GTK4 (Fase 6) toca solo `ewgui_view_gtk4.c`; los módulos no
  cambian (siguen usando `ewgui_canvas_*`).
- `EwGuiCanvas` debe exponer lo necesario para el cacheo (tamaño, redibujado
  parcial con `ewgui_canvas_queue_draw_rect`) — ya disponible.
- Riesgo: si se elige mal (cairo por frame), el port es más lento que GTK3.
  Mitigación: medir con `make -C ew_gui_tools/common bench` + sysprof antes de
  decidir el escape a `GtkGLArea`.

## Alternativas consideradas

- **cairo por frame en `GtkDrawingArea`**: descartado (regresión de rendimiento).
- **`GtkGLArea` desde el inicio**: más rendimiento potencial, más complejidad
  (shaders/contexto GL, texto con Pango fuera de GL); se reserva como escape.
- **Cambiar de toolkit** (Qt/ImGui): fuera de alcance (el objetivo es mantener C
  y el ecosistema GTK; ver `docs/GTK4-MIGRATION.md`).
