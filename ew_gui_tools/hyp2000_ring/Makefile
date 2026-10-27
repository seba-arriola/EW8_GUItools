# Makefile para hyp2000_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con HYPOINVERSE-2000.
# Enlaza el motor vendorizado en hypoinverse/ (libhypoinverse.a, gfortran).
# Autocontenido, como csnloc: no depende de src/ de EarthWorm.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -I../common/include -I../csnloc -I../csnhypodbp -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

HYPOLIB = hypoinverse/libhypoinverse.a

TARGET = hyp2000_ring

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(HYPOLIB):
	$(MAKE) -C hypoinverse

LIVE_OBJS = ../csnhypodbp/csnhypodbp_group.o \
            ../csnhypodbp/csnhypodbp_locator.o \
            ../csnloc/pick_scln.o ../csnloc/pick_buffer.o

../csnhypodbp/csnhypodbp_group.o: ../csnhypodbp/csnhypodbp_group.c \
                                  ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnhypodbp/csnhypodbp_locator.o: ../csnhypodbp/csnhypodbp_locator.c \
                                    ../csnhypodbp/csnhypodbp_locator.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/pick_scln.o: ../csnloc/pick_scln.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/pick_buffer.o: ../csnloc/pick_buffer.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): hyp2000_ring.o $(LIVE_OBJS) $(HYPOLIB)
	$(CC) $(CFLAGS) -o $(TARGET) hyp2000_ring.o $(LIVE_OBJS) $(HYPOLIB) $(EWLIBS) \
	      -lgfortran $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Invariantes de fuente (sin motor): la clave se lee y ZTR va tras el .hyp.
.PHONY: check
check:
	sh ./test/test_fixdepth_invariants.sh
	sh ./test/test_live_invariants.sh

# Prueba real offline (necesita EW_PARAMS + csnloc compilado).
.PHONY: check-offline
check-offline: $(TARGET)
	bash ./test/test_fixdepth.sh

clean:
	-$(MAKE) -C hypoinverse clean
	-rm -f *.o $(TARGET)

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
