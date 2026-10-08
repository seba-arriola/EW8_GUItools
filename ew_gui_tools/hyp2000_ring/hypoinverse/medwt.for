      SUBROUTINE MEDWT (LMED, N, K, KW, MED, MSD)
C--COMPUTES THE WEIGHTED MEDIAN OF N VALUES (MAGNITUDES). THE VALUES ARE IN K
C  AND THE WEIGHTS IN KW. THE MEDIAN IS RETURNED IN MED.
C--EACH WEIGHT KW MUST BE LARGER THAN 0, BUT NEED NOT BE NORMALIZED.
C--K AND KW ARE REARRANGED IN THIS PROCESS.
      LOGICAL      LMED       !T FOR WEIGHTED MEDIAN, F FOR MEAN
      INTEGER      N          !THE NUMBER OF VALUES DEFINED IN K & KW.
      INTEGER      K(N)       !ARRAY OF VALUES.
      INTEGER      KW(N)      !WEIGHTS OF THE VALUES OF K.
      INTEGER      MED        !THE RESULTING MEDIAN OF N VALUES OF K.
      INTEGER      MSD        !THE RMS (STANDARD DEV) OF MED FOR MEAN ONLY

      MSD=0
C--HANDLE THE TRIVIAL CASE OF N=1
      IF (N.LT.2) THEN
        MED=K(1)
        RETURN
      END IF

C--WEIGHTED MEDIAN
      IF (LMED) THEN
C--FIRST SORT THE VALUES OF K IN ASCENDING ORDER
        CALL SORT_MAGNITUDES (N,K,KW)

C--GET THE TOTAL WEIGHT THEN DIVIDE BY 2
        KX=KW(1)
        DO I=2,N
          KX=KX+KW(I)
        END DO
        X=KX*.5

C--FIND THE INDEX I OF THE WEIGHT AT OR BELOW THE HALFWAY POINT
        KX=KW(1)
        DO I=2,N
          IF (KX+.5*KW(I) .GT. X) GOTO 30
          KX=KX+KW(I)
        END DO

30      I=I-1
C--HALF THE TOTAL WEIGHT X IS NOW AT I OR BETWEEN I & I+1
C  THE CUMULATIVE WEIGHT AT I   IS NOW KX-.5*KW(I)
C  THE CUMULATIVE WEIGHT AT I+1 IS NOW KX+.5*KW(I+1)
C--NOW USE LINEAR INTERPOLATION BETWEEN I AND I+1
        MED= NINT( K(I) + (K(I+1)-K(I)) * (2.*(X-KX) + KW(I)) /
     2 (KW(I+1)+KW(I)) )
      
      ELSE
C--MEAN AND SD (ACTUALLY RMS OF MAGNITUDES)
        KD=KW(1)*K(1)
        KX=KW(1)
        DO I=2,N
          KD=KD+KW(I)*K(I)
          KX=KX+KW(I)
        END DO
        IF (KX.EQ.0) WRITE (6,*) ' *** EVENT MAGNITUDE HAS 0 WEIGHT'
        S=KD
        MED=NINT(S/KX)

        S=(KW(1)*(K(1)-MED))**2
        T=KW(1)**2
        DO I=2,N
          S=S+(KW(I)*(K(I)-MED))**2
          T=T+KW(I)**2
        END DO
        MSD=NINT(SQRT(S/T))
      END IF
      RETURN
      END
C---------------------------------------------------------------------
      subroutine SORT_MAGNITUDES(count,kdata,kdata2)
c
c     this routine bubble sorts the given kdata, 
c     and it passively sorts kdata2
c
      integer, intent (IN)    :: count
      integer, intent (INOUT) :: kdata(count)
      integer, intent (INOUT) :: kdata2(count)

      integer i
      integer pass  
      logical sorted 
      integer ktemp
      integer ktemp2

      pass = 1
 1    continue
      sorted = .TRUE.
      do i = 1,count-pass
        if(kdata(i) .gt. kdata(i+1)) then
          ktemp = kdata(i)
          ktemp2 = kdata2(i)
          kdata(i) = kdata(i+1)
          kdata2(i) = kdata2(i+1)
          kdata(i+1) = ktemp
          kdata2(i+1) = ktemp2
          sorted = .FALSE.
        endif
      end do
      pass = pass + 1
      if(.not. sorted) goto 1 
      return
      end
