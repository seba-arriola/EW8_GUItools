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
            ../csnloc/pick_scln.o ../csnloc/pick_buffer.o \
            ../csnloc/ttmodel.o ../csnloc/stations.o ../csnloc/live_assoc.o \
            ../csnloc/live_check.o ../csnloc/taulib_csnloc.o

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

../csnloc/ttmodel.o: ../csnloc/ttmodel.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/stations.o: ../csnloc/stations.c ../csnloc/csnloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/live_assoc.o: ../csnloc/live_assoc.c ../csnloc/live_assoc.h \
                        ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/live_check.o: ../csnloc/live_check.c ../csnloc/live_assoc.h
	$(CC) $(CFLAGS) -c -o $@ $<

../csnloc/taulib_csnloc.o: ../csnloc/taulib_csnloc.c ../csnloc/taulib_compat.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): hyp2000_ring.o $(LIVE_OBJS) $(HYPOLIB)
	$(CC) $(CFLAGS) -o $(TARGET) hyp2000_ring.o $(LIVE_OBJS) $(HYPOLIB) $(EWLIBS) \
	      -lgfortran $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Contrato de live_assoc: P/S por tiempo de viaje + validacion de salida.
test_live_assoc: test/test_live_assoc.c $(LIVE_OBJS)
	$(CC) $(CFLAGS) -Itest -o $@ test/test_live_assoc.c $(LIVE_OBJS) $(LIBS)
	@chmod +x $@

# Invariantes de fuente (sin motor): la clave se lee y ZTR va tras el .hyp.
.PHONY: check
check: test_live_assoc
	sh ./test/test_fixdepth_invariants.sh
	sh ./test/test_live_invariants.sh
	./test_live_assoc

# Prueba real offline (necesita EW_PARAMS + csnloc compilado).
.PHONY: check-offline
check-offline: $(TARGET) test/rebuild_arc
	bash ./test/test_fixdepth.sh

# Puente ARC real -> builder canonico (ver test/rebuild_arc.c).
test/rebuild_arc: test/rebuild_arc.c ../csnhypodbp/csnhypodbp_group.o \
                  ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -Itest -o $@ test/rebuild_arc.c \
	      ../csnhypodbp/csnhypodbp_group.o $(LIBS)

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
