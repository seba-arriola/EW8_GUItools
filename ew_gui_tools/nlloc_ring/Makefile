# Makefile para nlloc_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con NonLinLoc (NLLoc).
# Enlaza el motor vendorizado en nlloc/ (libnlloc.a). Autocontenido.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -I../common/include -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

NLLLIB = nlloc/libnlloc.a

TARGET = nlloc_ring

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(NLLLIB):
	$(MAKE) -C nlloc

$(TARGET): nlloc_ring.o nlloc_depth.o $(NLLLIB)
	$(CC) $(CFLAGS) -o $(TARGET) nlloc_ring.o nlloc_depth.o $(NLLLIB) $(EWLIBS) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Profundidad fija (emulada): reescritura pura de LOCGRID / LOCSEARCH.
test/test_fixdepth: test/test_fixdepth.c nlloc_depth.c nlloc_depth.h
	$(CC) $(CFLAGS) -o $@ test/test_fixdepth.c nlloc_depth.c -lm

.PHONY: check check-offline
check: test/test_fixdepth
	./test/test_fixdepth

# Prueba real offline (necesita EW_PARAMS, csnloc y los recursos de NLLoc).
check-offline: $(TARGET)
	bash ./test/test_fixdepth_offline.sh

clean:
	-$(MAKE) -C nlloc clean
	-rm -f *.o $(TARGET) test/test_fixdepth

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
