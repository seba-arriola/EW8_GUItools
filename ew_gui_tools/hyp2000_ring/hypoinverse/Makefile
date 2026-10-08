# Libreria estatica de HYPOINVERSE-2000 (Klein) vendorizada para hyp2000_ring.
#
# Fuente: EarthWorm (gitlab.com/seismic-software/earthworm),
#         src/seismic_processing/hyp2000 + hyp2000_mgr (hypoinv.for, wrapper).
# Se compila con gfortran como Fortran77 de forma fija (los .for) + un wrapper
# ISO_C_BINDING (.f90) que expone `void hypoinv_wrapper(const char*, int*)`.
#
#   make            -> hypoinverse/libhypoinverse.a
#   make clean

FC      = gfortran
FFLAGS  = -O2 -g -std=legacy -w -ffixed-line-length-none -fdollar-ok -fno-automatic -I.

F90_SRCS = hypoinv_wrapper.f90
F77_SRCS = hypoinv.for \
           hybda.for hycmd.for hysta.for hydel.for hyate.for hyfmc.for \
           hycal.for hyxmc.for hycrh.for hycrt.for hystl.for hyopen.for \
           hyinit.for hyphs.for hysou.for hytrl.for hyloc.for hysol.for \
           hysvd.f hytra.for hytrh.for hytrt.for hymag.for hyrep.for \
           hylst.for hysum.for hyinp.for hypro.for medwt.for utmcal.for \
           hydelt.for hyedit.for hytime.for hybeg.f hypref.for hymagp.for \
           linv.for linvol.for hyfile.for hylinv.for hylin.for hycre.for \
           hytre.for trvcon.for allsubs.f hydatum.for

OBJS = $(F90_SRCS:.f90=.o) $(F77_SRCS:.for=.o)
OBJS := $(OBJS:.f=.o)

all: libhypoinverse.a

libhypoinverse.a: $(OBJS)
	ar rcs $@ $(OBJS)

%.o: %.f90
	$(FC) $(FFLAGS) -c $< -o $@

%.o: %.for
	$(FC) $(FFLAGS) -c $< -o $@

%.o: %.f
	$(FC) $(FFLAGS) -c $< -o $@

clean:
	rm -f *.o *.mod libhypoinverse.a
