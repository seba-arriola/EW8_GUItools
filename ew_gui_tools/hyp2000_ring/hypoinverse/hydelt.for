      SUBROUTINE HYDELT (STR1,LEN1,STR2,LEN2)
C--DELETES A FILE FOR HYPOINVERSE USED WITH INTERACTIVE PROCESSING
C--PORT LINUX/GFORTRAN: no-op. Solo se usaba en el flujo interactivo VMS
C  (LIB$DELETE_FILE). En modo batch no se invoca con efecto.
      CHARACTER STR1*(*),STR2*(*)
      RETURN
      END
