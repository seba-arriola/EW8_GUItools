# Makefile para pickS
#
# Picker automático de onda S (Earthworm nativo).
# Enlaza contra libew_mt.a como el resto de los csn*.

CFLAGS = -O2 -g -Wall -I. -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = pickS.o config.o sta_list.o filters.o ring3c.o stalta.o aic.o \
       polarization.o quality.o report.o picks_ref.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

TARGET = pickS

# Todos los objetos dependen del header (evita layouts de struct inconsistentes).
# El primer target del fichero es esta regla de dependencias: sin fijar el goal,
# `make` a secas compilaria objetos y NUNCA enlazaria ni instalaria el binario.
.DEFAULT_GOAL := all

$(OBJS): pickS.h
$(TESTS): pickS.h

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(EWLIBS) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

TESTS = test/test_filter test/test_stalta test/test_aic \
        test/test_polarization test/test_quality test/test_ring3c \
        test/test_report test/test_sta_list test/gen_tank

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_filter ===";       ./test/test_filter
	@echo "=== test_stalta ===";       ./test/test_stalta
	@echo "=== test_aic ===";          ./test/test_aic
	@echo "=== test_polarization ==="; ./test/test_polarization
	@echo "=== test_quality ===";      ./test/test_quality
	@echo "=== test_ring3c ===";       ./test/test_ring3c
	@echo "=== test_report ===";       ./test/test_report
	@echo "=== test_sta_list ===";     ./test/test_sta_list
	@echo "TODOS LOS TESTS OK"

test/test_filter: test/test_filter.c filters.o
	$(CC) $(CFLAGS) -o $@ test/test_filter.c filters.o $(LIBS)

test/test_stalta: test/test_stalta.c stalta.o
	$(CC) $(CFLAGS) -o $@ test/test_stalta.c stalta.o $(LIBS)

test/test_aic: test/test_aic.c aic.o
	$(CC) $(CFLAGS) -o $@ test/test_aic.c aic.o $(LIBS)

test/test_polarization: test/test_polarization.c polarization.o
	$(CC) $(CFLAGS) -o $@ test/test_polarization.c polarization.o $(LIBS)

test/test_quality: test/test_quality.c quality.o config.o
	$(CC) $(CFLAGS) -o $@ test/test_quality.c quality.o config.o $(EWLIBS) $(LIBS)

test/test_ring3c: test/test_ring3c.c ring3c.o filters.o stalta.o
	$(CC) $(CFLAGS) -o $@ test/test_ring3c.c ring3c.o filters.o stalta.o $(LIBS)

test/test_report: test/test_report.c report.o picks_ref.o
	$(CC) $(CFLAGS) -o $@ test/test_report.c report.o picks_ref.o $(LIBS)

test/test_sta_list: test/test_sta_list.c sta_list.o
	$(CC) $(CFLAGS) -o $@ test/test_sta_list.c sta_list.o $(LIBS)

test/gen_tank: test/gen_tank.c
	$(CC) $(CFLAGS) -o $@ test/gen_tank.c $(LIBS)

# Test integrado del modo offline (determinista, sin anillos ni Earthworm).
check-offline: $(TARGET) test/gen_tank
	sh ./test/test_offline.sh

clean:
	rm -f *.o $(TARGET) $(TESTS) test/*.o

.PHONY: all clean test check check-offline

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
