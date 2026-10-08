# Libreria estatica de NonLinLoc (Lomax) vendorizada para nlloc_ring.
#
# Fuente: github.com/ut-beg-texnet/NonLinLoc (GPL-3.0). Se compilan los objetos
# de GRID_LIB_OBJS + NLLOC_LIB_OBJS + NLLoc1 (los mismos que usa NLLoc_func_test
# en el CMakeLists original) y se exponen la funcion `NLLoc()`.
#
#   make            -> nlloc/libnlloc.a
#   make clean

CC = cc
CFLAGS = -O2 -g -w -D_GNU_SOURCE -fcommon \
         -Iinclude -Ialomax_matrix -Igeometry -Imatrix_statistics \
         -Iran1 -Iocttree -Iio -Iio/jReadWrite -I.

GRID_SRCS = GridLib.c util.c geo.c octtree/octtree.c io/json_io.c \
            io/jReadWrite/source/jRead.c io/jReadWrite/source/jWrite.c \
            alomax_matrix/alomax_matrix.c alomax_matrix/eigv.c \
            alomax_matrix/alomax_matrix_svd.c \
            matrix_statistics/matrix_statistics.c vector/vector.c \
            ran1/ran1.c map_project.c

NLLOC_SRCS = calc_crust_corr.c velmod.c NLLocLib.c GridMemLib.c \
             phaselist.c loclist.c otime_limit.c NLLoc1.c

OBJS = $(GRID_SRCS:.c=.o) $(NLLOC_SRCS:.c=.o)

all: libnlloc.a

libnlloc.a: $(OBJS)
	ar rcs $@ $(OBJS)

# Binarios para precalcular grillas (se usan una vez, no en runtime).
Vel2Grid: Vel2Grid1.o $(OBJS)
	$(CC) -o $@ Vel2Grid1.o $(OBJS) -lm

Grid2Time: Grid2Time1.o Time_3d_NLL_multiSource.o $(OBJS)
	$(CC) -o $@ Grid2Time1.o Time_3d_NLL_multiSource.o $(OBJS) -lm

# El CMake original compila este fichero con -DNO_IEEE_PROTOCOL (evita `nint`).
Time_3d_NLL_multiSource.o: Time_3d_NLL_multiSource.c
	$(CC) $(CFLAGS) -DNO_IEEE_PROTOCOL -c $< -o $@

grids: Vel2Grid Grid2Time

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libnlloc.a Vel2Grid Grid2Time Vel2Grid1.o Grid2Time1.o Time_3d_NLL_multiSource.o
