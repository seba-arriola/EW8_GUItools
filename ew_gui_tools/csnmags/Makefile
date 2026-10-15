# Makefile para csnmags
#
# Motor de magnitudes ML/Mwp/Mb/Ms. Módulo C nativo con dos modos (argv):
#   csnmags <csnmags.d>                                   -> anillo
#   csnmags <csnmags.d> <hypos.jsonl> <tank> [--out F]    -> offline
#
# Respuesta instrumental autocontenida (lector PZ + FFT propia): no enlaza
# transfer.o. Enlaza contra libew_mt.a como el resto de los csn*.

CFLAGS = -O2 -g -Wall -I. -I../common/include -I../wavestore \
         -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = csnmags.o mag_config.o mag_sta.o mag_formula.o mag_amp.o \
       mag_calib.o mag_response.o mag_io.o mag_out.o wave_source.o mag_key.o

# Fuente SDS compartida (`ewgui_sds_path`) + libmseed2 estatica: la misma que usa
# csnhypodbp para leer el archivo SeisComP.
SDSOBJ = ewgui_sds.o $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a

# Objetos sueltos de EarthWorm que no viven en libew_mt.a
EWLIB  = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
WSLIB  = ../wavestore/libwavestore.a
EWOBJ  = $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o

TARGET = csnmags

# El primer target del fichero es esta regla de dependencias: sin fijar el goal,
# `make` a secas compilaria objetos y NUNCA enlazaria ni instalaria el binario.
.DEFAULT_GOAL := all

$(OBJS): csnmags.h
$(TESTS): csnmags.h

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

../wavestore/libwavestore.a:
	$(MAKE) -C ../wavestore libwavestore.a

$(TARGET): $(OBJS) $(SDSOBJ) ../wavestore/libwavestore.a
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(WSLIB) $(EWOBJ) $(EWLIB) $(SDSOBJ) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

# Lector SDS compartido (common/src/ewgui_sds.c): se compila aqui, en local.
ewgui_sds.o: ../common/src/ewgui_sds.c
	$(CC) $(CFLAGS) -c -o $@ ../common/src/ewgui_sds.c

# ---- tests headless (sin anillos ni Earthworm en ejecución) ----
TESTS = test/test_formula test/test_amp test/test_pz test/test_config \
        test/test_event_key test/gen_tank test/gen_hypos

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_formula ==="; ./test/test_formula
	@echo "=== test_amp ===";     ./test/test_amp
	@echo "=== test_pz ===";      ./test/test_pz
	@echo "=== test_config ===";  ./test/test_config
	@echo "=== test_event_key ==="; ./test/test_event_key
	@echo "=== test_offline ==="; sh ./test/test_offline.sh
	@echo "TODOS LOS TESTS OK"

test/test_formula: test/test_formula.c mag_formula.o
	$(CC) $(CFLAGS) -o $@ test/test_formula.c mag_formula.o $(LIBS)

test/test_amp: test/test_amp.c mag_amp.o mag_formula.o
	$(CC) $(CFLAGS) -o $@ test/test_amp.c mag_amp.o mag_formula.o $(LIBS)

test/test_pz: test/test_pz.c mag_response.o
	$(CC) $(CFLAGS) -o $@ test/test_pz.c mag_response.o $(LIBS)

test/test_config: test/test_config.c mag_config.o
	$(CC) $(CFLAGS) -o $@ test/test_config.c mag_config.o $(EWLIB) $(LIBS)

test/test_event_key: test/test_event_key.c mag_key.o
	$(CC) $(CFLAGS) -o $@ test/test_event_key.c mag_key.o $(LIBS)

test/gen_tank: test/gen_tank.c
	$(CC) $(CFLAGS) -o $@ test/gen_tank.c $(LIBS)

test/gen_hypos: test/gen_hypos.c
	$(CC) $(CFLAGS) -o $@ test/gen_hypos.c $(LIBS)

check-offline: $(TARGET) test/gen_tank test/gen_hypos
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
