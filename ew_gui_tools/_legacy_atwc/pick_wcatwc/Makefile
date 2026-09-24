# Obtiene el nombre de la carpeta (ej. loc_wcatwc)
MODULE = $(notdir $(CURDIR))

CC = gcc
# Banderas de compilacion (sin GTK)
CFLAGS = -Wall -O2 -I../../libsrc -I$(EW_HOME)/$(EW_VERSION)/include -fcommon -D_LINUX -D_REENTRANT -D_INTEL

EW_SRC_UTIL = $(EW_HOME)/$(EW_VERSION)/src/libsrc/util
EW_SRC_UNIX = $(EW_HOME)/$(EW_VERSION)/src/libsrc/unix

# Librerias base de Earthworm necesarias para ATWC
EW_OBJS = $(EW_SRC_UNIX)/transport.o \
          $(EW_SRC_UTIL)/getutil.o \
          $(EW_SRC_UTIL)/kom.o \
          $(EW_SRC_UNIX)/sleep_ew.o \
          $(EW_SRC_UNIX)/time_ew.o \
          $(EW_SRC_UTIL)/logit_mt.o \
          $(EW_SRC_UNIX)/sema_ew.o \
          $(EW_SRC_UNIX)/threads_ew.o \
          $(EW_SRC_UTIL)/swap.c

LDFLAGS = $(EW_OBJS) -lm -lpthread

# Busca todos los .c en libsrc y en la carpeta actual
ATWC_SRCS = $(wildcard ../../libsrc/*.c)
MOD_SRCS = $(wildcard *.c)

all: $(MODULE)

$(MODULE): $(MOD_SRCS) $(ATWC_SRCS)
	$(CC) $(CFLAGS) -o $(MODULE) $(MOD_SRCS) $(ATWC_SRCS) $(LDFLAGS)
	cp $(MODULE) $(EW_HOME)/$(EW_VERSION)/bin/
	@echo "======================================================"
	@echo " EXITO: $(MODULE) compilado y copiado a EW bin!"
	@echo "======================================================"

clean:
	rm -f $(MODULE)
