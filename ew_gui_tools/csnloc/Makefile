# Makefile para csnloc
#
# Módulo Earthworm nativo de asociación de fases + localización rápida.
# Autocontenido: incluye su propia copia del motor IASP91 (taulib_csnloc.c),
# sus headers (iasplib.h/ttlim.h) y las tablas (iasp91.tbl/.hed), sin depender
# de ew_gui_tools/libsrc. Enlaza contra libew_mt.a como el resto de los csn*.

# Los headers iasplib.h/ttlim.h viven en este directorio y se resuelven por
# include con comillas (busqueda relativa al .c), por eso NO se agrega -I.
CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = csnloc.o pick_scln.o pick_buffer.o stations.o ttmodel.o grid.o \
       gridset.o backprojection.o dbscan.o refine.o hypo_out.o

# taulib_csnloc.c es una copia local del motor IASP91 con el PATCH NULL-safe
# de strstr/strchr (bug de 64 bits que corrompia memoria al sustituir fases).
# No requiere complex.c: la aritmetica compleja no se usa en este camino.
TAUOBJS = taulib_csnloc.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

TARGET = csnloc

all: $(TARGET)

$(TARGET): $(OBJS) $(TAUOBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(TAUOBJS) $(EWLIBS) $(LIBS)
	cp $(TARGET) $(EW_HOME)/$(EW_VERSION)/bin/

# Copia local de taulib.c con el parche NULL-safe de strstr.
taulib_csnloc.o: taulib_csnloc.c taulib_compat.h
	$(CC) $(CFLAGS) -c taulib_csnloc.c -o $@

# Tests unitarios (standalone, sin EarthWorm).
TESTS = test/test_pick_parse test/test_repick test/test_tt \
        test/test_dbscan test/test_hypo_format test/test_synth_locate \
        test/test_two_events test/test_grid_parse test/test_gridset_load \
        test/test_precompute_equiv test/test_grid_activation \
        test/test_nested_dedup

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

# Solo los tests de grillas anidadas (diagnostico rapido).
check-grids: test/test_grid_parse test/test_gridset_load \
             test/test_precompute_equiv test/test_grid_activation \
             test/test_nested_dedup iasp91.tbl iasp91.hed
	@echo "=== test_grid_parse ===";       ./test/test_grid_parse
	@echo "=== test_gridset_load ===";     ./test/test_gridset_load
	@echo "=== test_precompute_equiv ==="; ./test/test_precompute_equiv
	@echo "=== test_grid_activation ===";  ./test/test_grid_activation
	@echo "=== test_nested_dedup ===";     ./test/test_nested_dedup

# Las tablas IASP91 viven en este directorio (los tests las abren desde '.').

# Ejecuta la bateria completa.
check: $(TESTS) iasp91.tbl iasp91.hed
	@echo "=== test_pick_parse ===";       ./test/test_pick_parse
	@echo "=== test_repick ===";           ./test/test_repick
	@echo "=== test_dbscan ===";           ./test/test_dbscan
	@echo "=== test_hypo_format ===";      ./test/test_hypo_format
	@echo "=== test_tt ===";               ./test/test_tt
	@echo "=== test_synth_locate ===";     ./test/test_synth_locate
	@echo "=== test_two_events ===";       ./test/test_two_events
	@echo "=== test_grid_parse ===";       ./test/test_grid_parse
	@echo "=== test_gridset_load ===";     ./test/test_gridset_load
	@echo "=== test_precompute_equiv ==="; ./test/test_precompute_equiv
	@echo "=== test_grid_activation ===";  ./test/test_grid_activation
	@echo "=== test_nested_dedup ===";     ./test/test_nested_dedup
	@echo "TODOS LOS TESTS OK"

test/test_pick_parse: test/test_pick_parse.c pick_scln.o
	$(CC) $(CFLAGS) -o $@ test/test_pick_parse.c pick_scln.o $(LIBS)

test/test_repick: test/test_repick.c pick_buffer.o
	$(CC) $(CFLAGS) -o $@ test/test_repick.c pick_buffer.o $(LIBS)

test/test_dbscan: test/test_dbscan.c dbscan.o
	$(CC) $(CFLAGS) -o $@ test/test_dbscan.c dbscan.o $(LIBS)

test/test_tt: test/test_tt.c ttmodel.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_tt.c ttmodel.o $(TAUOBJS) $(LIBS)

test/test_hypo_format: test/test_hypo_format.c hypo_out.o stations.o pick_scln.o
	$(CC) $(CFLAGS) -o $@ test/test_hypo_format.c hypo_out.o stations.o \
	      pick_scln.o $(LIBS)

test/test_synth_locate: test/test_synth_locate.c backprojection.o dbscan.o \
                        refine.o grid.o ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_synth_locate.c backprojection.o \
	      dbscan.o refine.o grid.o ttmodel.o stations.o $(TAUOBJS) $(LIBS)

test/test_two_events: test/test_two_events.c backprojection.o dbscan.o \
                      refine.o grid.o ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_two_events.c backprojection.o \
	      dbscan.o refine.o grid.o ttmodel.o stations.o $(TAUOBJS) $(LIBS)

test/test_grid_parse: test/test_grid_parse.c grid.o ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_grid_parse.c grid.o ttmodel.o \
	      stations.o $(TAUOBJS) $(LIBS)

test/test_gridset_load: test/test_gridset_load.c gridset.o grid.o ttmodel.o \
                        stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_gridset_load.c gridset.o grid.o \
	      ttmodel.o stations.o $(TAUOBJS) $(LIBS)

test/test_precompute_equiv: test/test_precompute_equiv.c backprojection.o \
                            dbscan.o refine.o gridset.o grid.o ttmodel.o \
                            stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_precompute_equiv.c backprojection.o \
	      dbscan.o refine.o gridset.o grid.o ttmodel.o stations.o \
	      $(TAUOBJS) $(LIBS)

test/test_grid_activation: test/test_grid_activation.c gridset.o grid.o \
                           ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_grid_activation.c gridset.o grid.o \
	      ttmodel.o stations.o $(TAUOBJS) $(LIBS)

test/test_nested_dedup: test/test_nested_dedup.c backprojection.o dbscan.o \
                        refine.o gridset.o grid.o ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/test_nested_dedup.c backprojection.o \
	      dbscan.o refine.o gridset.o grid.o ttmodel.o stations.o \
	      $(TAUOBJS) $(LIBS)

# Generador de picks sinteticos para el test del modo offline.
test/gen_offline_picks: test/gen_offline_picks.c ttmodel.o stations.o $(TAUOBJS)
	$(CC) $(CFLAGS) -o $@ test/gen_offline_picks.c ttmodel.o stations.o \
	      $(TAUOBJS) $(LIBS)

# Diagnostico ASan del modulo completo: make asan-csnloc PICKS=<archivo.picks>
asan-csnloc:
	$(CC) -O1 -g -Wall -fsanitize=address -fno-omit-frame-pointer \
	      -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
	      -o csnloc_asan csnloc.c pick_scln.c pick_buffer.c stations.c \
	      ttmodel.c grid.c gridset.c backprojection.c dbscan.c refine.c \
	      hypo_out.c taulib_csnloc.c $(EWLIBS) $(LIBS)
	cd $(EW_PARAMS) && ASAN_OPTIONS=detect_leaks=0 $(CURDIR)/csnloc_asan csnloc.d $(PICKS)

# Test del modo offline (reloj virtual). Requiere EW_PARAMS/EW_LOG:
#   source ../../ew8_unix.sh && make check-offline
check-offline: $(TARGET) test/gen_offline_picks iasp91.tbl iasp91.hed
	@if [ -z "$$EW_PARAMS" ]; then \
	  echo "check-offline: SKIP (falta EW_PARAMS; haz 'source ../../ew8_unix.sh')"; \
	else \
	  ./test/test_offline.sh; \
	fi

clean:
	rm -f *.o $(TARGET) csnloc_asan $(TESTS) test/gen_offline_picks \
	      test/test_offline.picks test/test_tt_asan

# Diagnostico: compila el test de TT con AddressSanitizer y lo ejecuta.
asan-tt: iasp91.tbl iasp91.hed
	$(CC) -O1 -g -Wall -fsanitize=address -fno-omit-frame-pointer \
	      -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
	      -o test/test_tt_asan test/test_tt.c ttmodel.c \
	      taulib_csnloc.c -lm -lpthread
	./test/test_tt_asan 2>&1 | head -60

# Diagnostico con gdb si esta disponible.
gdb-tt: test/test_tt iasp91.tbl iasp91.hed
	gdb -batch -ex run -ex bt --args ./test/test_tt 2>&1 | tail -40

# Diagnostico con trazas del ensamblado de candidatos.
dbg-synth: test/test_synth_locate iasp91.tbl iasp91.hed
	CSNLOC_DEBUG=1 ./test/test_synth_locate 2>&1 | head -80

dbg-two: test/test_two_events iasp91.tbl iasp91.hed
	CSNLOC_DEBUG=1 ./test/test_two_events 2>&1 | head -80

# Smoke: comprueba que el binario arranca y muestra su uso (sin anillos).
smoke: $(TARGET)
	-./$(TARGET)

# Determinismo: repite los tests de localizacion varias veces.
repeat: $(TESTS) iasp91.tbl iasp91.hed
	@for i in 1 2 3 4 5; do \
	  ./test/test_synth_locate >/dev/null 2>&1 && \
	  ./test/test_two_events >/dev/null 2>&1 && \
	  echo "run $$i OK" || echo "run $$i FALLO"; \
	done

# Diagnostico ASan del localizador sintetico.
asan-synth: iasp91.tbl iasp91.hed
	$(CC) -O1 -g -Wall -fsanitize=address -fno-omit-frame-pointer \
	      -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
	      -o test/test_synth_asan test/test_synth_locate.c backprojection.c \
	      dbscan.c refine.c grid.c ttmodel.c stations.c \
	      taulib_csnloc.c -lm -lpthread
	./test/test_synth_asan 2>&1 | head -60

.PHONY: all clean test check check-grids check-offline asan-csnloc
