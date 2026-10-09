# Makefile para csnmags
#
# Motor de magnitudes ML/Mwp/Mb/Ms. Módulo C nativo con dos modos (argv):
#   csnmags <csnmags.d>                                   -> anillo
#   csnmags <csnmags.d> <hypos.jsonl> <tank> [--out F]    -> offline
#
# Respuesta instrumental autocontenida (lector PZ + FFT propia): no enlaza
# transfer.o. Enlaza contra libew_mt.a como el resto de los csn*.

CFLAGS = -O2 -g -Wall -I. -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = csnmags.o mag_config.o mag_sta.o mag_formula.o mag_amp.o \
       mag_calib.o mag_response.o mag_io.o mag_out.o wave_source.o

# Objetos sueltos de EarthWorm que no viven en libew_mt.a
EWLIB  = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
EWOBJ  = $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o

TARGET = csnmags

$(OBJS): csnmags.h
$(TESTS): csnmags.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(EWOBJ) $(EWLIB) $(LIBS)
	cp $(TARGET) $(EW_HOME)/$(EW_VERSION)/bin/

# ---- tests headless (sin anillos ni Earthworm en ejecución) ----
TESTS = test/test_formula test/test_amp test/test_pz test/test_config \
        test/gen_tank test/gen_hypos

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_formula ==="; ./test/test_formula
	@echo "=== test_amp ===";     ./test/test_amp
	@echo "=== test_pz ===";      ./test/test_pz
	@echo "=== test_config ===";  ./test/test_config
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

test/gen_tank: test/gen_tank.c
	$(CC) $(CFLAGS) -o $@ test/gen_tank.c $(LIBS)

test/gen_hypos: test/gen_hypos.c
	$(CC) $(CFLAGS) -o $@ test/gen_hypos.c $(LIBS)

check-offline: $(TARGET) test/gen_tank test/gen_hypos
	sh ./test/test_offline.sh

clean:
	rm -f *.o $(TARGET) $(TESTS) test/*.o

.PHONY: all clean test check check-offline
