# Makefile para csnhypodbp (Visor de Hipocentros GTK4 + libadwaita + Earthworm 8) - MODULAR

CC = gcc
GUI_PKG = gtk4 libadwaita-1

# Flags de compilacion para GTK4 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

# Librerias nativas de Earthworm 8
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

# Objetos modulares (el DSP vive ahora en libewgui)
OBJS = csnhypodbp_main.o csnhypodbp_gui.o csnhypodbp_ws.o csnhypodbp_row.o

all: csnhypodbp

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

csnhypodbp: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o csnhypodbp $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp csnhypodbp $(EW_HOME)/$(EW_VERSION)/bin/

# Test unitario del filtro IIR y manejo de gaps (DSP puro, sin GTK/EW link)
test_filter: test_filter.c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o test_filter test_filter.c ../common/libewgui_gtk4.a -lm

test: test_filter
	./test_filter

clean:
	rm -f csnhypodbp test_filter *.o
