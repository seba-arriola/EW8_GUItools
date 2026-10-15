#ifndef SCNL_TABLE_H
#define SCNL_TABLE_H

#include "trace_buf.h"
#include <stddef.h>
#include <stdio.h>

#define INCREMENT_SCNL 10 // increment for scnl table

typedef struct {
  char sta[TRACE2_STA_LEN];   /* Site name (NULL-terminated) */
  char net[TRACE2_NET_LEN];   /* Network name (NULL-terminated) */
  char chan[TRACE2_CHAN_LEN]; /* Component/channel code (NULL-terminated)*/
  char loc[TRACE2_LOC_LEN];   /* Location code (NULL-terminated) */
} SCNL_ENTRY;

typedef struct {
  int nSCNL;
  int maxSCNL;
  SCNL_ENTRY *scnl_entries;
} SCNL_TABLE;

/**
 * Deterrmines if the character is a terminating character for SCNL.
 * @param c the character
 * @return true if the character is a terminating character, false otherwise.
 */
int is_scnl_term(char c);

/**
 * Add an entry to the SCNL table.
 * @param scnl_table the SCNL table.
 * @return a pointer to the empty new SCNL entry.
 */
SCNL_ENTRY *scnl_table_add(SCNL_TABLE *scnl_table);

/**
 * Copy the  string for a SCNL.
 * @param dest the destination string.
 * @param dest_size the size of the destination string.
 * @param src the source string.
 * @param len the lenth of the source string.
 */
void scnl_table_strcpy(char *dest, size_t dest_size, char *src, int len);

/**
 * Resize the SCNL table.
 * @param scnl_table the SCNL table.
 * @param maxSCNL the maximum number of SCNL entries.
 */
void scnl_table_resize(SCNL_TABLE *scnl_table, int maxSCNL);

/**
 * Set the entry by parsing the specified string.
 * @param scnl_entry the SCNL entry to set.
 * @param str the string to parse the SCNL.
 * @return a pointer in the string after the SCNL.
 */
char *scnl_table_set_entry(SCNL_ENTRY *scnl_entry, char *str);

/**
 * Determines the number of lines in the file and sets the table size
 * accordingly.
 * @param scnl_table the SCNL table.
 * @param fp the file pointer.
 */
void scnl_table_size(SCNL_TABLE *scnl_table, FILE *fp);

#endif
