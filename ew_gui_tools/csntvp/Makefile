# Makefile para csntvp (GTK4 + libadwaita + Earthworm 8)

CC = gcc
GUI_PKG = gtk4 libadwaita-1
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

LIBS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

OBJS = csntvp_main.o csntvp_data.o csntvp_gui.o

# Dejamos que libew_mt.a maneje kom, logit, getutil, transport, etc.
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

csntvp: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o csntvp $(OBJS) $(EWLIBS) $(LIBS)
	cp csntvp $(EW_HOME)/$(EW_VERSION)/bin/

# Unit test for the ported aplicar_filtro_iir (standalone, no EW/GTK deps)
test: test_filter
	./test_filter

test_filter: test_filter.c
	$(CC) -O2 -o test_filter test_filter.c -lm

clean:
	rm -f *.o csntvp test_filter
