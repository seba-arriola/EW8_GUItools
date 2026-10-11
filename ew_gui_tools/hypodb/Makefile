# Makefile para hypodb — ingester de información hipocentral a PostgreSQL.
#
# Headless (sin GTK). Reutiliza el núcleo puro de csnhypodbp (agrupación) y dos
# fuentes de common (config kom + capa libpq) compiladas localmente para no
# arrastrar GTK. Ver hipodb.d y db/README.md.

CFLAGS = -O2 -g -Wall -Wextra -I. -I../common/include -I../csnhypodbp \
         -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
         $(shell pkg-config --cflags libpq)

LIBS   = -lm -lpthread $(shell pkg-config --libs libpq)
EWLIB  = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
EWOBJ  = $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o

LIB     = libhypodb.a
LIBOBJS = hypodb_ids.o hypodb_config.o hypodb_ingest.o \
          csnhypodbp_group.o ewgui_config.o ewgui_db.o

TESTS = test_ids test_group_events test_ingest_db

all: $(LIB) install

$(LIB): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

# Fuentes reutilizadas (compiladas en local para no depender de GTK).
csnhypodbp_group.o: ../csnhypodbp/csnhypodbp_group.c ../csnhypodbp/csnhypodbp_group.h
	$(CC) $(CFLAGS) -c -o $@ $<

ewgui_config.o: ../common/src/ewgui_config.c ../common/include/ewgui/config.h
	$(CC) $(CFLAGS) -c -o $@ $<

ewgui_db.o: ../common/src/ewgui_db.c ../common/include/ewgui/db.h
	$(CC) $(CFLAGS) -c -o $@ $<

hypodb_ingest.o: hypodb_ingest.h hypodb_config.h hypodb_ids.h

hypodb_ingest: hypodb_ingest_main.o $(LIB)
	$(CC) $(CFLAGS) -o $@ hypodb_ingest_main.o $(LIB) $(EWOBJ) $(EWLIB) $(LIBS)

install: hypodb_ingest
	cp -p hypodb_ingest $(EW_HOME)/$(EW_VERSION)/bin/

test_ids: test_ids.c hypodb_ids.o
	$(CC) $(CFLAGS) -o $@ test_ids.c hypodb_ids.o $(LIBS)

test_group_events: test_group_events.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_group_events.c $(LIB) $(EWOBJ) $(EWLIB) $(LIBS)

test_ingest_db: test_ingest_db.c $(LIB)
	$(CC) $(CFLAGS) -o $@ test_ingest_db.c $(LIB) $(EWOBJ) $(EWLIB) $(LIBS)

test: $(TESTS)

check: $(TESTS)
	@echo "=== test_ids ===";          ./test_ids
	@echo "=== test_group_events ==="; ./test_group_events
	@echo "=== test_ingest_db ===";    ./test_ingest_db
	@echo "TODOS LOS TESTS OK"

clean:
	rm -f *.o $(LIB) $(TESTS) hypodb_ingest

.PHONY: all test check clean install
