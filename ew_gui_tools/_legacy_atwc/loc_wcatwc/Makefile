MODULE = $(notdir $(CURDIR))
CC = gcc

# Banderas mágicas: Tolerancia absoluta a 64 bits y supresión de advertencias
CFLAGS = -Wall -O2 -g -I../../libsrc -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT -D_INTEL -fno-stack-protector -U_FORTIFY_SOURCE -Wno-format

EW_LIB = $(EW_HOME)/$(EW_VERSION)/lib
EW_SRC_UTIL = $(EW_HOME)/$(EW_VERSION)/src/libsrc/util
EW_SRC_UNIX = $(EW_HOME)/$(EW_VERSION)/src/libsrc/unix

EWLIBS = $(EW_SRC_UTIL)/logit_mt.o \
         $(EW_SRC_UNIX)/time_ew.o \
         $(EW_SRC_UNIX)/transport.o \
         $(EW_SRC_UNIX)/sleep_ew.o \
         $(EW_SRC_UTIL)/getutil.o \
         $(EW_SRC_UTIL)/kom.o \
         $(EW_SRC_UNIX)/sema_ew.o \
         $(EW_SRC_UNIX)/threads_ew.o \
         $(EW_SRC_UTIL)/swap.c

LDFLAGS = $(EWLIBS) -lm -lpthread

# ATWCLIBS: Librerias base de ATWC necesarias
ATWCLIBS = ../../libsrc/logquake.c \
           ../../libsrc/locquake.c \
           ../../libsrc/dummy.c \
           ../../libsrc/mags.c \
           ../../libsrc/taulib.c \
           ../../libsrc/geotools.c \
           ../../libsrc/mjstime.c \
           ../../libsrc/report.c \
           ../../libsrc/get_pick.c \
           ../../libsrc/get_hypo.c \
           ../../libsrc/filters.c \
           ../../libsrc/complex.c \
           ../../libsrc/ReadStationData.c \
           ../../libsrc/fereg.c \
           ../../libsrc/littoral.c \
           ../../libsrc/diskrw.c

# Archivos locales del modulo
LOCAL_SRCS = loc_wcatwc.c config.c locate.c

all: $(MODULE)

$(MODULE): $(LOCAL_SRCS)
	$(CC) $(CFLAGS) -o $(MODULE) $(LOCAL_SRCS) $(ATWCLIBS) $(LDFLAGS)
	cp $(MODULE) $(EW_HOME)/$(EW_VERSION)/bin/
	@echo "======================================================"
	@echo " EXITO: $(MODULE) compilado y copiado a EW bin!"
	@echo "======================================================"

clean:
	rm -f $(MODULE) *.o
