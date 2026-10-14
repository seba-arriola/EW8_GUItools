# Makefile para csnwphase — inversión de fase W on-demand (motor eost/wphase).
#
# Headless (sin GTK): no es un módulo de anillos, no se declara en
# startstop_unix.d. Ver csnwphase.h y docs/WPHASE.md.

CFLAGS = -O2 -g -Wall -Wextra -I. -I../common/include -I../wavestore \
         -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread
EWLIB  = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
# libwavestore referencia el cliente de anillos (wavesource.o), así que hay que
# aportar esos objetos igual que hace csnmags.
EWOBJ  = $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o
WSLIB  = ../wavestore/libwavestore.a
SDSLIB = ewgui_sds.o $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a
WDEPS  = $(WSLIB) $(SDSLIB) $(EWOBJ)

LIB     = libcsnwphase.a
LIBOBJS = csnwphase_cfg.o csnwphase_prep.o csnwphase_pz.o csnwphase_sac.o \
          csnwphase_wave.o csnwphase_run.o csnwphase_parse.o csnwphase_log.o

TESTS = test_cfg test_prep test_parse test_pz test_sac

all: $(LIB) install

# ewgui_sds.o es entrada de enlace (no va dentro de la lib): se declara como
# prerequisito para que make lo compile antes de enlazar.
$(LIB): $(LIBOBJS) ewgui_sds.o
	$(AR) rcs $@ $(LIBOBJS)

csnwphase_prep.o: csnwphase_prep.c csnwphase.h
csnwphase_cfg.o: csnwphase_cfg.c csnwphase.h
csnwphase_pz.o: csnwphase_pz.c csnwphase.h
csnwphase_sac.o: csnwphase_sac.c csnwphase.h
csnwphase_wave.o: csnwphase_wave.c csnwphase.h
csnwphase_run.o: csnwphase_run.c csnwphase.h
csnwphase_parse.o: csnwphase_parse.c csnwphase.h
csnwphase_log.o: csnwphase_log.c csnwphase.h

# Fuentes reutilizadas de common (compiladas en local).
ewgui_sds.o: ../common/src/ewgui_sds.c ../common/include/ewgui/sds.h
	$(CC) $(CFLAGS) -c -o $@ $<

csnwphase: csnwphase_main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ csnwphase_main.o $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

.PHONY: install
install: csnwphase
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin csnwphase

test_cfg: test_cfg.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_cfg.c $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

test_prep: test_prep.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_prep.c $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

test_parse: test_parse.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_parse.c $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

test_pz: test_pz.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_pz.c $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

test_sac: test_sac.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_sac.c $(LIB) $(WDEPS) $(EWLIB) $(LIBS)

test: $(TESTS)

check: $(TESTS)
	@echo "=== test_cfg ===";   ./test_cfg
	@echo "=== test_prep ===";  ./test_prep
	@echo "=== test_parse ==="; ./test_parse
	@echo "=== test_pz ===";    ./test_pz /tmp/csnwphase_pz || test $$? -eq 2
	@echo "=== test_sac ===";   ./test_sac /tmp/csnwphase_sac
	@echo "=== engine: PZ (AC4) ===";  bash tests/test_pz_engine.sh
	@echo "=== engine: SAC (AC3) ==="; bash tests/test_sac_engine.sh
	@echo "=== ondas: SDS real (M7.4 2024-07-19) ==="; bash tests/test_wave_sds.sh
	@echo "=== orquestacion: run.sh (dry) ==="; bash tests/test_run_dry.sh
	@echo "TODOS LOS TESTS OK"

# Fixtures de referencia generados con el Python original del CSN.
fixtures:
	bash tools/gen_fixtures.sh

clean:
	rm -f *.o $(LIB) $(TESTS) csnwphase

.PHONY: all test check clean install fixtures
