# Makefile para wavestore — store de formas de onda indexado (headless).
#
# La libreria core (wstore*.c) NO usa libew_mt; solo el ingester/config lo hace.
# Como es un archivo .a, el linker solo arrastra lo referenciado: los tests del
# core no necesitan Earthworm.

CFLAGS = -O2 -g -Wall -Wextra -I. -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread
EWLIB  = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

LIB     = libwavestore.a
LIBOBJS = wstore.o wstore_index.o wstore_io.o wstore_writer.o \
          wstore_config.o wstore_ingest.o wstore_filter.o wavesource.o

CONVBINS = wstore_import wstore_export wstore_query wstore_prune gen_traces
TESTS    = test_wstore test_wstore_concurrent test_ingest test_wavesource \
           test_filter test_config test_prune

all: $(LIB) install wsbench

$(LIB): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

$(LIBOBJS): wstore.h wstore_internal.h
wstore_writer.o: wstore_writer.h
wstore_config.o: wstore_config.h
wstore_ingest.o: wstore_ingest.h wstore_config.h wstore_writer.h wstore_filter.h
wstore_filter.o: wstore_filter.h
wavesource.o: wavesource.h

wstore_ingest: wstore_ingest_main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ wstore_ingest_main.o $(LIB) $(EWLIB) $(LIBS)

# Instala en EW_HOME/bin de forma atómica y SIEMPRE. No lo hace `check`.
.PHONY: install
install: wstore_ingest $(CONVBINS)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin wstore_ingest wstore_import wstore_export wstore_query wstore_prune

wstore_import: wstore_import.c $(LIB)
	$(CC) $(CFLAGS) -o $@ wstore_import.c $(LIB) $(LIBS)

wstore_export: wstore_export.c $(LIB)
	$(CC) $(CFLAGS) -o $@ wstore_export.c $(LIB) $(LIBS)

wstore_query: wstore_query.c $(LIB)
	$(CC) $(CFLAGS) -o $@ wstore_query.c $(LIB) $(LIBS)

# Purga por CLI: mismo nucleo que la horaria del ingester, para poder limpiar
# (o medir con --dry-run) sin depender de que el modulo este corriendo.
wstore_prune: wstore_prune.c $(LIB)
	$(CC) $(CFLAGS) -o $@ wstore_prune.c $(LIB) $(LIBS)

gen_traces: gen_traces.c
	$(CC) $(CFLAGS) -o $@ gen_traces.c $(LIBS)

wsbench: wsbench.c $(LIB)
	$(CC) $(CFLAGS) -o $@ wsbench.c $(LIB) $(EWLIB) $(LIBS)

test_wstore: test_wstore.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_wstore.c $(LIB) $(LIBS)

test_wstore_concurrent: test_wstore_concurrent.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_wstore_concurrent.c $(LIB) $(LIBS)

test_ingest: test_ingest.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_ingest.c $(LIB) $(EWLIB) $(LIBS)

test_wavesource: test_wavesource.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_wavesource.c $(LIB) $(EWLIB) $(LIBS)

# El filtro no usa Earthworm: no hace falta $(EWLIB).
test_filter: test_filter.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_filter.c $(LIB) $(LIBS)

test_config: test_config.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_config.c $(LIB) $(EWLIB) $(LIBS)

# La purga tampoco usa Earthworm: solo lee nombres de fichero.
test_prune: test_prune.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_prune.c $(LIB) $(LIBS)

test: $(TESTS) $(CONVBINS)

check: $(TESTS) $(CONVBINS)
	@echo "=== test_wstore ===";            ./test_wstore
	@echo "=== test_wstore_concurrent ==="; ./test_wstore_concurrent
	@echo "=== test_ingest ===";            ./test_ingest
	@echo "=== test_wavesource ===";        ./test_wavesource
	@echo "=== test_filter ===";            ./test_filter
	@echo "=== test_config ===";            ./test_config
	@echo "=== test_prune ===";            ./test_prune
	@echo "=== invariantes de fuente ==="; sh ./test_source_invariants.sh
	@echo "=== test_convert ===";           sh ./test/test_convert.sh
	@echo "TODOS LOS TESTS OK"

clean:
	rm -f *.o $(LIB) $(TESTS) wstore_ingest $(CONVBINS) wsbench

.PHONY: all test check clean install

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
