#include <stdio.h>
#include <string.h>
#include <kom.h>
#include <transport.h>
#include <earthworm.h>
#include <trace_buf.h>
#include "disk_wcatwc.h"

#define NCOMMAND 11          /* Number of commands in the config file */


 /***********************************************************************
  * GetConfig()                            *
  * Processes command file using kom.c functions.           *
  * Returns -1 if any errors are encountered.             *
  ***********************************************************************/

int GetConfig( char *config_file, GPARM *Gparm )
{
   char     iInit[NCOMMAND];     /* Flags, one for each command */
   int      iNumMiss;            /* Number of commands that were missed */
   int      iNumFiles;           /* Number of config files */
   int      i;

/* Set to zero one iInit flag for each required command
   ****************************************************/
   for ( i = 0; i < NCOMMAND; i++ ) iInit[i] = 0;

/* Open the main configuration file
   ********************************/
   iNumFiles = k_open( config_file );
   if ( iNumFiles == 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error opening configuration file <%s>\n",
               config_file );
      return -1;
   }

/* Process all nested configuration files
   **************************************/
   while ( iNumFiles > 0 )       /* While there are config files open */
   {
      while ( k_rd() )           /* Read next line from active file  */
      {
         int  success;
         char *com;
         char *str;

         com = k_str();          /* Get the first token from line */

         if ( !com ) continue;             /* Ignore blank lines */
         if ( com[0] == '#' ) continue;    /* Ignore comments */

/* Open another configuration file
   *******************************/
         if ( com[0] == '@' )
         {
            success = iNumFiles + 1;
            iNumFiles  = k_open( &com[1] );
            if ( iNumFiles != success )
            {
               fprintf( stderr, "disk_wcatwc: Error opening .d file <%s>.\n",
                        &com[1] );
               return -1;
            }
            continue;
         }

/* Read configuration parameters
   *****************************/
         else if ( k_its( "StaFile" ) )
         {
            if ( (str = k_str()) )
               strcpy( Gparm->StaFile, str );
            iInit[0] = 1;
         }

         else if ( k_its( "StaDataFile" ) )
         {
            if ( (str = k_str()) )
               strcpy( Gparm->StaDataFile, str );
            iInit[1] = 1;
         }
		
         else if ( k_its( "ResponseFile" ) )
         {
            if ( (str = k_str()) )
               strcpy( Gparm->ResponseFile, str );
            iInit[10] = 1;
         }

         else if ( k_its( "InRing" ) )
         {
            if ( (str = k_str()) )
            {
               if( (Gparm->InKey = GetKey(str)) == -1 )
               {
                  fprintf( stderr, "disk_wcatwc: Invalid InRing name <%s>. "
				                   "Exiting.\n", str );
                  return -1;
               }
            }
            iInit[2] = 1;
         }

         else if ( k_its( "HeartbeatInt" ) )
         {
            Gparm->HeartbeatInt = k_int();
            iInit[3] = 1;
         }

         else if ( k_its( "Debug" ) )
         {
            Gparm->Debug = k_int();
            iInit[4] = 1;
         }

         else if ( k_its( "MyModId" ) )
         {
            if ( (str = k_str()) )
            {
               if ( GetModId(str, &Gparm->MyModId) == -1 )
               {
                  fprintf( stderr, "disk_wcatwc: Invalid MyModId <%s>.\n",str );
                  return -1;
               }
            }
            iInit[5] = 1;
         }

         else if ( k_its( "FileLength" ) )
         {
            Gparm->FileLength = k_int();
            iInit[6] = 1;
         }

         else if ( k_its( "CircDeleteHours" ) )
         {
            Gparm->CircDeleteHours = k_int();
            iInit[7] = 1;
         }

         else if ( k_its( "DiskWritePath" ) )
         {
            if ( (str = k_str()) )
               strcpy( Gparm->DiskWritePath, str );
            iInit[8] = 1;
         }

         else if ( k_its( "FileSuffix" ) )
         {
            if ( (str = k_str()) )
               strcpy( Gparm->FileSuffix, str );
            iInit[9] = 1;
         }

/* An unknown parameter was encountered
   ************************************/
         else
         {
            fprintf( stderr, "disk_wcatwc: <%s> unknown parameter in <%s>\n",
                    com, config_file );
            continue;
         }

/* See if there were any errors processing the command
   ***************************************************/
         if ( k_err() )
         {
            fprintf( stderr, "disk_wcatwc: Bad <%s> command in <%s>.\n", com,
                     config_file );
            return -1;
         }
      }
      iNumFiles = k_close();
   }

/* After all files are closed, check flags for missed commands
   ***********************************************************/
   iNumMiss = 0;
   for ( i = 0; i < NCOMMAND; i++ )
      if ( !iInit[i] )
         iNumMiss++;

   if ( iNumMiss > 0 )
   {
      fprintf( stderr, "disk_wcatwc: ERROR, no " );
      if ( !iInit[0] ) fprintf( stderr, "<StaFile> " );
      if ( !iInit[1] ) fprintf( stderr, "<StaDataFile> " );
      if ( !iInit[2] ) fprintf( stderr, "<InRing> " );
      if ( !iInit[3] ) fprintf( stderr, "<HeartbeatInt> " );
      if ( !iInit[4] ) fprintf( stderr, "<Debug> " );
      if ( !iInit[5] ) fprintf( stderr, "<MyModId> " );
      if ( !iInit[6] ) fprintf( stderr, "<FileLength> " );
      if ( !iInit[7] ) fprintf( stderr, "<CircDeleteHours> " );
      if ( !iInit[8] ) fprintf( stderr, "<DiskWritePath> " );
      if ( !iInit[9] ) fprintf( stderr, "<FileSuffix> " );
      if ( !iInit[10] ) fprintf( stderr, "<ResponseFile> " );
      fprintf( stderr, "command(s) in <%s>. Exiting.\n", config_file );
      return -1;
   }
   return 0;
}


 /***********************************************************************
  * LogConfig()                            *
  * *
  * Log the configuration parameters                  *
  ***********************************************************************/

void LogConfig( GPARM *Gparm )
{
   logit( "", "\n" );
   logit( "", "StaFile:         %s\n",    Gparm->StaFile );
   logit( "", "StaDataFile:     %s\n",    Gparm->StaDataFile );
   logit( "", "InKey:           %6d\n",   Gparm->InKey );
   logit( "", "MyModId:         %6u\n",   Gparm->MyModId );
   logit( "", "HeartbeatInt:    %6d\n",   Gparm->HeartbeatInt );
   logit( "", "FileLength:      %6d\n",   Gparm->FileLength );
   logit( "", "CircDeleteHours: %6d\n",   Gparm->CircDeleteHours );
   logit( "", "DiskWritePath:   %s\n",    Gparm->DiskWritePath );
   logit( "", "FileSuffix:      %s\n",    Gparm->FileSuffix );
   logit( "", "ResponseFile:    %s\n",    Gparm->ResponseFile );
   logit( "", "Debug:           %6d\n\n", Gparm->Debug );
   return;
}
