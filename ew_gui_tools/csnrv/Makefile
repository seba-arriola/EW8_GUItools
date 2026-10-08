# Makefile para csnrv (Centro Sismologico Nacional - Report Viewer)

CC = gcc

# Módulo migrado a GTK4 + libadwaita (Fase 6). Enlaza libewgui_gtk4.a.
GUI_PKG = gtk4 libadwaita-1

CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

OBJS = csnrv.o

# Libreria base nativa de Earthworm 8 (contiene logit, kom, tport, time_ew, etc.)
# rw_mag.o aporta rd_mag() para parsear TYPE_MAGNITUDE del anillo.
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: csnrv

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

csnrv: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o csnrv $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp csnrv $(EW_HOME)/$(EW_VERSION)/bin/

# Test headless de configuracion (no requiere DISPLAY)
test: csnrv
	BIN=./csnrv bash ./test_config.sh
	./csnrv --test-mag

.PHONY: all test clean

clean:
	rm -f csnrv *.o
