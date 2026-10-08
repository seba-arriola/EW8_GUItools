   Subroutine HYPOINV_WRAPPER( c_string, c_result ) &
   Bind ( C, Name='hypoinv_wrapper' )

!
!  Subroutine HYPOINV_WRAPPER is a wrapper for the C hyp2000_mgr to call
!  Fortran Subroutine HYPOINV.  Subroutine HYPOINV is the Subroutne
!  version of Program HYPOINVERSE.  Subroutine HYPOINV_WRAPPER is not a
!  standard part of Program HYPOINVERSE.
!
!  The C function prototype is:
!
!     void hypoinv_wrapper( const char *, int * );
!
!  Subroutine HYPOINV_WRAPPER uses the Fortran 2003 BIND ( C ) attribute
!  and Intrinsic Module ISO_C_BINDING.
!

   Use, Intrinsic :: ISO_C_BINDING, Only: C_INT, C_CHAR, C_NULL_CHAR

   Implicit None

!
!  Procedure prototypes
!

   Interface

      Subroutine HYPOINV( cmdstr, iresr )
         Implicit None
         Character, Intent( In  ) :: cmdstr*(*)
         Integer,   Intent( Out ) :: iresr
      End Subroutine

   End Interface

   Integer, Parameter :: STRLEN = 80

!
!  Arguments to this Subroutine
!

   Character ( Kind=C_CHAR ), Intent( IN  ) :: c_string(*)
   Integer   ( Kind=C_INT  ), Intent( OUT ) :: c_result

!
!  Local variables
!

   Integer   i
   Character string*(STRLEN)
   Integer   result


!
!  Copy the incoming C string to a FORTRAN string
!

   string = ' '
   Do i = 1, STRLEN
      If ( c_string(i) == C_NULL_CHAR ) Then
         Exit
      End If
      string(i:i) = c_string(i)
   End Do

!  i = i - 1
!  Write (*,*) '--->HYPOINV_WRAPPER: string*(', i, ') = "', string(1:i), '"'

!
!  Call the Subroutine version of Program HYPOINVERSE.
!  c_result contains the result code.
!

   Call HYPOINV( string, result )

!  Write (*,*) '--->HYPOINV_WRAPPER: result = ', result

   c_result = result

   Return
   End
