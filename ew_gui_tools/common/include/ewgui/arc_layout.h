/******************************************************************************
 * arc_layout.h                                                               *
 *                                                                            *
 * Contrato de bytes compartido del mensaje TYPE_HYP2000ARC.                  *
 * Lo incluyen csnloc, hyp2000_ring, nlloc_ring y csnhypodbp para no duplicar  *
 * la definicion de la marca de solucion manual.                              *
 *                                                                            *
 * Marca de solucion manual (>=1 picada manual) en la LINEA 1 del ARC:        *
 *   offset 0-based 174 = campo eventID[4] (read_arc.h:214).                  *
 *   - La linea 1 de csnloc mide 197 bytes (hypo_out.c ARC_LINE1) y su memset  *
 *     deja 174 en blanco; ningun consumidor del proyecto lee 174 ni 197.     *
 *   - 'M' = solucion con al menos una picada manual; 'A' = solo automaticas. *
 *   - Ausente/en blanco debe interpretarse como automatica.                  *
 ******************************************************************************/
#ifndef EWGUI_ARC_LAYOUT_H
#define EWGUI_ARC_LAYOUT_H

#define EWGUI_ARC_MANUAL_OFF   174
#define EWGUI_ARC_MANUAL_CHAR  'M'
#define EWGUI_ARC_AUTO_CHAR    'A'

#endif /* EWGUI_ARC_LAYOUT_H */
