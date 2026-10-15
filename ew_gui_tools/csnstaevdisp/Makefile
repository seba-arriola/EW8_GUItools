# Makefile para csnstaevdisp (Visor de Estaciones y Eventos - GTK3 + Earthworm 8)
# Carga de estaciones y de la historia de sismos propia (sin libsrc/ATWC).

MODULE = csnstaevdisp

CC = gcc

# Módulo migrado a GTK4 + libadwaita (Fase 6). Enlaza libewgui_gtk4.a.
GUI_PKG = gtk4 libadwaita-1

# Flags GTK4 + Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) $(shell pkg-config --cflags libpq) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -fcommon -D_LINUX -D_REENTRANT -D_INTEL -Wno-format

# Librerias base de Earthworm: la vendorizada libew_mt.a ya trae los objetos
# que antes se tomaban de src/libsrc/{util,unix} (transport, getutil, kom,
# sleep_ew, time_ew, logit_mt, sema_ew, threads_ew). No hace falta src/ ni
# compilar swap.c al vuelo (libew_mt.a no referencia ningun simbolo swap).
EW_OBJS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) $(shell pkg-config --libs libpq) -lm -lpthread

all: $(MODULE) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(MODULE)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(MODULE)

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

$(MODULE): $(MODULE).c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o $(MODULE) $(MODULE).c ../common/libewgui_gtk4.a $(EW_OBJS) $(LDFLAGS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)
	@echo "======================================================"
	@echo " EXITO: $(MODULE) compilado y copiado a EW bin!"
	@echo "======================================================"

clean:
	rm -f $(MODULE) *.o test/test_data test/csnstaevdisp_nomain.o

# Verifica los parsers de estaciones e historia sin abrir la GUI.
PARAMS = ../../run_working_v8/params
test-data: test/test_data
	./test/test_data $(PARAMS)/estaciones_107.txt

# Verifica el parseo de config/overlays en modo headless (sin DISPLAY).
test-config: $(MODULE)
	BIN=./$(MODULE) bash ./test_config.sh

test/test_data: test/test_data.c $(MODULE).c
	$(CC) $(CFLAGS) -Dmain=csnstaevdisp_unused_main -c $(MODULE).c \
	      -o test/csnstaevdisp_nomain.o
	$(CC) $(CFLAGS) -o $@ test/test_data.c test/csnstaevdisp_nomain.o \
	      ../common/libewgui_gtk4.a $(EW_OBJS) $(LDFLAGS)

.PHONY: all clean test-data test-config

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
