#ifndef EWGUI_TABLE_UI_H
#define EWGUI_TABLE_UI_H

#include <stddef.h>

/* Helper de tablas ordenables por cabecera (capa de vista GTK4).
 *
 * Este header NO incluye <gtk/gtk.h> (tests/check_no_gtk.sh): los tipos de GTK
 * se declaran hacia adelante, igual que en menu_ui.h. */

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkColumnView GtkColumnView;
typedef struct _GtkColumnViewColumn GtkColumnViewColumn;
typedef struct _GtkSortListModel GtkSortListModel;
typedef struct _GtkSorter GtkSorter;

/* Direccion del primer clic (mismos valores que GtkSortType). */
enum { EWGUI_TABLE_ASC = 0, EWGUI_TABLE_DESC = 1 };

/* Compara dos textos de celda: numerico si AMBOS parsean como numero (el numero
 * tiene que consumir todo el texto), si no alfabetico sin distinguir
 * mayusculas. Un valor ausente ("", "-") cuenta como 0, asi que en descendente
 * queda al final. Pura, sin GTK: tests/test_table_cmp.c. */
int ewgui_table_cmp_values(const char *a, const char *b);

/* Marca una columna YA CREADA como ordenable por la cabecera. Ordena por la
 * propiedad `sort_prop` del item (string, double o int; NaN o vacio = 0) y
 * desempata por `tiebreak_prop` si no es NULL. `first_dir` es la direccion del
 * PRIMER clic: DESC en numericas/fecha, ASC en texto. */
void ewgui_table_column_sortable(GtkColumnViewColumn *col, const char *sort_prop,
                                 const char *tiebreak_prop, int first_dir);

/* Engancha el sorter de la vista al GtkSortListModel y fija el orden inicial.
 * Sin esto las cabeceras no ordenan: GtkColumnView necesita que el sorter de
 * gtk_column_view_get_sorter() este puesto en el sort model que hay debajo. */
void ewgui_table_enable_sorting(GtkColumnView *view, GtkSortListModel *model,
                                GtkColumnViewColumn *initial, int initial_dir);

#endif /* EWGUI_TABLE_UI_H */
