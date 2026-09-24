# Makefile para csnstaevdisp (Visor de Estaciones y Eventos - GTK3 + Earthworm 8)
# Carga de estaciones y de la historia de sismos propia (sin libsrc/ATWC).

MODULE = csnstaevdisp

CC = gcc

# Flags GTK3 + Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags gtk+-3.0) -I$(EW_HOME)/$(EW_VERSION)/include -fcommon -D_LINUX -D_REENTRANT -D_INTEL -Wno-format

# Librerias base de Earthworm necesarias (objetos ya compilados)
EW_SRC_UTIL = $(EW_HOME)/$(EW_VERSION)/src/libsrc/util
EW_SRC_UNIX = $(EW_HOME)/$(EW_VERSION)/src/libsrc/unix

EW_OBJS = $(EW_SRC_UNIX)/transport.o \
          $(EW_SRC_UTIL)/getutil.o \
          $(EW_SRC_UTIL)/kom.o \
          $(EW_SRC_UNIX)/sleep_ew.o \
          $(EW_SRC_UNIX)/time_ew.o \
          $(EW_SRC_UTIL)/logit_mt.o \
          $(EW_SRC_UNIX)/sema_ew.o \
          $(EW_SRC_UNIX)/threads_ew.o \
          $(EW_SRC_UTIL)/swap.c

LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lm -lpthread

all: $(MODULE)

$(MODULE): $(MODULE).c
	$(CC) $(CFLAGS) -o $(MODULE) $(MODULE).c $(EW_OBJS) $(LDFLAGS)
	cp $(MODULE) $(EW_HOME)/$(EW_VERSION)/bin/
	@echo "======================================================"
	@echo " EXITO: $(MODULE) compilado y copiado a EW bin!"
	@echo "======================================================"

clean:
	rm -f $(MODULE) *.o test/test_data test/csnstaevdisp_nomain.o

# Verifica los parsers de estaciones e historia sin abrir la GUI.
PARAMS = ../../run_working_v8/params
test-data: test/test_data
	./test/test_data $(PARAMS)/estaciones_107.txt $(PARAMS)/csnhypodbp_hist.txt

test/test_data: test/test_data.c $(MODULE).c
	$(CC) $(CFLAGS) -Dmain=csnstaevdisp_unused_main -c $(MODULE).c \
	      -o test/csnstaevdisp_nomain.o
	$(CC) $(CFLAGS) -o $@ test/test_data.c test/csnstaevdisp_nomain.o \
	      $(EW_OBJS) $(LDFLAGS)

.PHONY: all clean test-data
