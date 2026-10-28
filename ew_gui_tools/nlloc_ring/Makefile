# Makefile para nlloc_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con NonLinLoc (NLLoc).
# Enlaza el motor vendorizado en nlloc/ (libnlloc.a). Autocontenido.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -I../common/include -I../csnhypodbp -I../csnloc -D_LINUX -D_REENTRANT

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

LOCATOR_OBJ = ../csnhypodbp/csnhypodbp_locator.o
CHECK_OBJ   = ../csnloc/live_check.o
LIVE_OBJS   = ../csnloc/live_refine.o ../csnloc/live_assoc.o \
              ../csnloc/pick_scln.o ../csnloc/pick_buffer.o \
              ../csnloc/ttmodel.o ../csnloc/stations.o \
              ../csnloc/taulib_csnloc.o ../csnhypodbp/csnhypodbp_group.o

../csnhypodbp/csnhypodbp_locator.o: ../csnhypodbp/csnhypodbp_locator.c \
                                    ../csnhypodbp/csnhypodbp_locator.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnhypodbp/csnhypodbp_group.o: ../csnhypodbp/csnhypodbp_group.c \
                                  ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/pick_scln.o: ../csnloc/pick_scln.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/pick_buffer.o: ../csnloc/pick_buffer.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/ttmodel.o: ../csnloc/ttmodel.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/stations.o: ../csnloc/stations.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/taulib_csnloc.o: ../csnloc/taulib_csnloc.c ../csnloc/taulib_compat.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/live_check.o: ../csnloc/live_check.c ../csnloc/live_assoc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/live_assoc.o: ../csnloc/live_assoc.c ../csnloc/live_assoc.h \
                        ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/live_refine.o: ../csnloc/live_refine.c ../csnloc/live_refine.h \
                         ../csnloc/live_assoc.h ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): nlloc_ring.o nlloc_depth.o $(LOCATOR_OBJ) $(CHECK_OBJ) $(LIVE_OBJS) $(NLLLIB)
	$(CC) $(CFLAGS) -o $(TARGET) nlloc_ring.o nlloc_depth.o $(LOCATOR_OBJ) \
	      $(CHECK_OBJ) $(LIVE_OBJS) $(NLLLIB) $(EWLIBS) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Profundidad fija (emulada): reescritura pura de LOCGRID / LOCSEARCH.
test/test_fixdepth: test/test_fixdepth.c nlloc_depth.c nlloc_depth.h
	$(CC) $(CFLAGS) -o $@ test/test_fixdepth.c nlloc_depth.c -lm

test/test_locsearch: test/test_locsearch.c nlloc_depth.c nlloc_depth.h
	$(CC) $(CFLAGS) -o $@ test/test_locsearch.c nlloc_depth.c -lm

.PHONY: check check-offline
check: test/test_fixdepth test/test_locsearch
	sh ./test/test_output_invariants.sh
	./test/test_fixdepth
	./test/test_locsearch

# Prueba real offline (necesita EW_PARAMS, csnloc y los recursos de NLLoc).
check-offline: $(TARGET)
	bash ./test/test_fixdepth_offline.sh

clean:
	-$(MAKE) -C nlloc clean
	-rm -f *.o $(TARGET) test/test_fixdepth test/test_locsearch

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
