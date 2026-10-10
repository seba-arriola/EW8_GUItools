/******************************************************************************
 * csnhypodbp_master.h                                                        *
 *                                                                            *
 * Tienda MAESTRA: EXACTAMENTE una fila por grupo (evento). La identidad es el *
 * grupo (gpointer opaco), NUNCA el qid de la solucion: un grupo puede         *
 * contener soluciones con qids distintos (csnloc re-detecta el mismo sismo)   *
 * y la preferida puede ser cualquiera de ellas. Keyear por qid hacia que, al  *
 * cambiar la preferida a una fila de otro qid, el upsert ya no la encontrara  *
 * y la re-agregara en cada mensaje (filas repetidas con el MISMO ID).        *
 *                                                                            *
 * Nucleo GLib puro (sin GTK); se testea headless.                            *
 ******************************************************************************/
#ifndef CSNHYPODBP_MASTER_H
#define CSNHYPODBP_MASTER_H

#include <glib-object.h>
#include <gio/gio.h>

#include "csnhypodbp_row.h"

/* Graba/lee la identidad de grupo en la fila maestra. */
void     csnhypodbp_master_tag(CsnhypodbpRow *row, gpointer group);
gpointer csnhypodbp_master_group(CsnhypodbpRow *row);

/* Indice de la fila del grupo en la tienda, o -1 si no esta. */
int      csnhypodbp_master_find(GListStore *store, gpointer group);

/* Deja EXACTAMENTE una fila del grupo: reemplaza la existente en su posicion o
 * la agrega; ademas colapsa duplicados preexistentes. Devuelve el indice final
 * (o -1 si arguments invalidos). */
int      csnhypodbp_master_upsert(GListStore *store, gpointer group, CsnhypodbpRow *row);

/* Quita la(s) fila(s) del grupo. Devuelve cuantas quito. */
int      csnhypodbp_master_remove(GListStore *store, gpointer group);

#endif /* CSNHYPODBP_MASTER_H */
