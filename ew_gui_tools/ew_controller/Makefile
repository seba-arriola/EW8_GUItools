# Makefile para ew_controller (Panel grafico de control de startstop - GTK4 + libadwaita + Earthworm 8)

CC = gcc
GUI_PKG = gtk4 libadwaita-1

# Flags de compilacion para GTK4 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

OBJS = ew_controller_main.o ew_controller_gui.o ew_controller_row.o

# Libreria base nativa de Earthworm 8 (contiene logit, kom, transport, getutil,
# sema_ew, threads_ew, sleep_ew, time_ew, etc. usados por ew_controller)
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: ew_controller install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: ew_controller
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin ew_controller

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

ew_controller: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o ew_controller $(OBJS) $(EWLIBS) $(LDFLAGS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Invariantes de fuente (grep, sin GTK ni display): tablas ordenables y refresco
# de estado. Ver test_ui_invariants.sh.
test:
	bash ./test_ui_invariants.sh

# Arnes GUI (Xvfb + AT-SPI) contra el EarthWorm EN MARCHA: orden por cabecera,
# estado que se refleja y reparto del espacio. Ver tests/test_sort_and_status.sh
# y docs/GUI-TESTS.md.
test-live: tests/fake_status
	bash ./tests/test_sort_and_status.sh
	bash ./tests/test_sort_and_status.sh --layout
	bash ./tests/test_sort_and_status.sh --select
	bash ./tests/test_sort_and_status.sh --dup

tests/fake_status: tests/fake_status.c
	$(CC) $(CFLAGS) -o $@ tests/fake_status.c $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a -lm -lpthread

.PHONY: all clean test test-live

clean:
	rm -f ew_controller *.o tests/fake_status

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
