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

/*
 * Modelo de velocidades / curvas de tiempos de llegada que uso el localizador
 * para esa solucion. Viaja en la LINEA 1, offsets 182..194 (13 chars). Mismo
 * estilo que la marca manual.
 *
 * PROHIBIDO ponerlo en 122..136: HYPOINVERSE lee la linea 1 como su resumen
 * (hyphs.for, FORMAT 1212 = `T123,2(A1,F3.2,F3.1)`) y en 122..136 espera las
 * magnitudes (BMTYPI/BMAGI/TEMPI). Un nombre como "iasp91" ahi hace abortar a
 * LOC con "Bad value during floating point read", y hyp2000_ring le pasa el ARC
 * de entrada VERBATIM (hyp2000_ring.c hypo_locate -> arcIn), asi que pierde el
 * evento. (El pad4[13] de read_arc.h no existe en el arbol: el lector real es
 * hyphs.for y usa ese tramo.)
 *
 * 182..194 queda fuera de la linea de HYPOINVERSE (179 chars; el FORMAT no lee
 * mas alla de 163) y de los campos del proyecto 136..145 (qid), 161, 174 (marca
 * manual) y 178..181 (version). La linea 1 mide 197 bytes (hypo_out.c
 * ARC_LINE1) y se normaliza a 197 en hyp2000_ring/nlloc_ring.
 *   csnloc       -> cfg->TauModel            (p.ej. "iasp91")
 *   hyp2000_ring -> clave VelocityModel      (p.ej. "CHILE_POTIN")
 *   nlloc_ring   -> banda ModelBand elegida  (p.ej. "N34-42_1.5k" / "CHILE_4k")
 * Lo lee hypodb_ingest -> origin.velocity_model.
 */
#define EWGUI_ARC_VMODEL_OFF   182
#define EWGUI_ARC_VMODEL_LEN   13

/*
 * Offsets LEGADOS del modelo (122..135) donde HYPOINVERSE espera magnitudes.
 * Solo sirven para que hyp2000_ring LIMPIE ese bloque de un ARC viejo antes de
 * darselo a LOC; no se escribe nunca ahi.
 */
#define EWGUI_ARC_VMODEL_LEGACY_OFF  122
#define EWGUI_ARC_VMODEL_LEGACY_END  136

/* BMTYPI: columna 123 (offset 122) = tipo de la magnitud preferida, un A1 del
 * formato de HYPOINVERSE (L=ML, B=Mb, S=Ms, W=Mwp). Es lo unico que se escribe
 * en ese tramo: BMAGI/TEMPI (124..129) siguen en blanco. */
#define EWGUI_ARC_BMTYPI_OFF         122

#endif /* EWGUI_ARC_LAYOUT_H */
