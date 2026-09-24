# Makefile para csnrv (Centro Sismologico Nacional - Report Viewer)

CC = gcc

# Usamos pkg-config para obtener automaticamente los flags de GTK3
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags gtk+-3.0) -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Usamos pkg-config para obtener las librerias a enlazar de GTK3
LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lm -lpthread

OBJS = csnrv.o

# Libreria base nativa de Earthworm 8 (contiene logit, kom, tport, time_ew, etc.)
# rw_mag.o aporta rd_mag() para parsear TYPE_MAGNITUDE del anillo.
EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: csnrv

csnrv: $(OBJS)
	$(CC) $(CFLAGS) -o csnrv $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp csnrv $(EW_HOME)/$(EW_VERSION)/bin/

# Test headless de configuracion (no requiere DISPLAY)
test: csnrv
	BIN=./csnrv bash ./test_config.sh
	./csnrv --test-mag

.PHONY: all test clean

clean:
	rm -f csnrv *.o
