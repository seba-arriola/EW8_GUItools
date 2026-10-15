#ifndef __ARCDB_H__
#define __ARCDB_H__

#ifndef __ARCDB_C__
#define _EXTERN_ extern
#else
#define _EXTERN_
#endif

#define ARCDB_OPEN_READONLY 0x00000001

typedef struct _ARCEVENTINFO {
   double mag;
   char eventID[16];
   long rowid;
   int update_count;
   long version;
   int flags;
} ARCEVENTINFO;

#define ARCEVENTINFO_CLEAR(A) (memset(A, 0, sizeof(ARCEVENTINFO)))

/**
 * Close the ARC  database.
 */
void arcdb_close(void);

/**
 * Initialiize amd then open a connection to the ARC database.
 * @param path the path to the database file
 * @return 0 if success, non-zero otherwise.
 */
int arcdb_init(char *path);

/**
* 0pen a connection to the ARC database.
* @return 0 if success, non-zero otherwise.
*/
int arcdb_open(void);

/**
 * Set the ARC database open flags.
 * @param flags the open flags or 0 for the default (reading and writing, and is created if it does not already exist)
 */
void arcdb_setflags(int flags);

/**
 * Shut down the ARC database.
 */
void arcdb_shutdown();

/**
*  Delete events from the ARC dtabase.
*  @param dateStr the date string  to delete.
*/
void arcdb_delete_events(char *dateStr);

/**
* Get the event information from  the ARC database.
* @param arcEventInfo the ARC Event Information
* Input is either eventID (if open for read only) or rowid
* Output is all
* @return 0 if success or non-zero  if error
*/
int arcdb_get_event(ARCEVENTINFO *arcEventInfo);

/**
* Save the event to the ARC database.
* @param arcEventInfo the ARC Event Information
* @return 0 if success or non-zero  if error
*/
int arcdb_save_event(ARCEVENTINFO *arcEventInfo);

#endif
