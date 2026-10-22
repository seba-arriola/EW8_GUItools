# Makefile para csntvp (GTK4 + libadwaita + Earthworm 8)

CC = gcc
GUI_PKG = gtk4 libadwaita-1
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

LIBS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

OBJS = csntvp_main.o csntvp_data.o csntvp_gui.o csntvp_util.o

# Dejamos que libew_mt.a maneje kom, logit, getutil, transport, etc.
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: csntvp install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: csntvp
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin csntvp

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common

csntvp: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o csntvp $(OBJS) $(EWLIBS) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Unit tests (standalone, no EW/GTK deps)
test: test_filter test_util
	./test_filter
	./test_util

test_filter: test_filter.c
	$(CC) -O2 -o test_filter test_filter.c -lm

test_util: test_util.c csntvp_util.c csntvp_util.h
	$(CC) -O2 -Wall -o test_util test_util.c csntvp_util.c -lm

.PHONY: all test clean

clean:
	rm -f *.o csntvp test_filter test_util

# --- Dependencias de cabeceras (ver AGENTS.md §5) ---------------------------
# Sin esto, cambiar un .h NO recompila los .o que lo incluyen: el modulo queda
# con un layout de structs viejo mientras la libreria se recompila, y la ABI se
# rompe en silencio (paso con csnhypodbp y libcsnwphase.a: el i_master salia con
# la memoria del struct equivocado y la inversion de fase W no arrancaba).
# OJO: los `.d` del modulo NO son dependencias (son configuraciones: el
# `ew_controller.d` del panel o los `.d` de los tests de csnloc/csnmags), asi que
# las deps van a `*.dep` para no colisionar nunca con ellos.
CFLAGS += -MMD -MP -MF $(@).dep
-include $(wildcard *.dep */*.dep)
clean: clean-deps
clean-deps:
	rm -f *.dep */*.dep
