#ifndef EWGUI_VERSION_H
#define EWGUI_VERSION_H

/*
 * Librería común de los módulos gráficos de ew_gui_tools (libewgui.a).
 *
 * Reglas de la librería (ver docs/GTK4-MIGRATION.md):
 *   - Las capas core/dsp/wave/geo NO incluyen gtk/gtk.h; se testean headless.
 *   - El único punto con GTK es la capa de vista (view_gtk*.c).
 *   - Los menús/acciones usan GIO (GMenu/GSimpleAction), portable a GTK4.
 */

const char *ewgui_version(void);

#endif /* EWGUI_VERSION_H */
