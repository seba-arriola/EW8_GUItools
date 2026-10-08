#ifndef EW_CONTROLLER_ROW_H
#define EW_CONTROLLER_ROW_H

#include <glib-object.h>

/* Filas GObject para GtkColumnView (GTK4): módulos y rings. */

#define EC_TYPE_MOD_ROW (ec_mod_row_get_type())
G_DECLARE_FINAL_TYPE(EcModRow, ec_mod_row, EC, MOD_ROW, GObject)
EcModRow *ec_mod_row_new(const char *name, int pid, const char *status, const char *details);

#define EC_TYPE_RING_ROW (ec_ring_row_get_type())
G_DECLARE_FINAL_TYPE(EcRingRow, ec_ring_row, EC, RING_ROW, GObject)
EcRingRow *ec_ring_row_new(const char *name, int key, int size);

#endif /* EW_CONTROLLER_ROW_H */
