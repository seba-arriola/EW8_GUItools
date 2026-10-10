# Makefile para csnhypodbp (Visor de Hipocentros GTK4 + libadwaita + Earthworm 8) - MODULAR

CC = gcc
GUI_PKG = gtk4 libadwaita-1

# Flags de compilacion para GTK4 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -I../csnloc -I../wavestore -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

# Librerias nativas de Earthworm 8
EWLIBS = ../common/libewgui_gtk4.a \
         ../csnloc/libcsnloc_tt.a \
         ../wavestore/libwavestore.a \
         $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

# Objetos modulares (el DSP vive ahora en libewgui)
OBJS = csnhypodbp_main.o csnhypodbp_gui.o csnhypodbp_ws.o csnhypodbp_row.o \
       csnhypodbp_group.o csnhypodbp_trace.o csnhypodbp_tt.o csnhypodbp_master.o \
       csnhypodbp_station.o csnhypodbp_locator.o csnhypodbp_manual.o csnhypodbp_locate.o \
       csnhypodbp_view.o

all: csnhypodbp

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

../csnloc/libcsnloc_tt.a:
	$(MAKE) -C ../csnloc libcsnloc_tt.a

../wavestore/libwavestore.a:
	$(MAKE) -C ../wavestore libwavestore.a

csnhypodbp: $(OBJS) ../common/libewgui_gtk4.a ../csnloc/libcsnloc_tt.a ../wavestore/libwavestore.a
	$(CC) $(CFLAGS) -o csnhypodbp $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp csnhypodbp $(EW_HOME)/$(EW_VERSION)/bin/

# Test unitario del filtro IIR y manejo de gaps (DSP puro, sin GTK/EW link)
test_filter: test_filter.c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o test_filter test_filter.c ../common/libewgui_gtk4.a -lm

# Motor puro de agrupacion + parseo (sin GTK/EW link)
test_group: test_group.c csnhypodbp_group.o
	$(CC) $(CFLAGS) -o test_group test_group.c csnhypodbp_group.o -lm

test_parse_solution: test_parse_solution.c csnhypodbp_group.o
	$(CC) $(CFLAGS) -o test_parse_solution test_parse_solution.c csnhypodbp_group.o -lm

test_magstore: test_magstore.c csnhypodbp_group.o
	$(CC) $(CFLAGS) -o test_magstore test_magstore.c csnhypodbp_group.o -lm

# Logica pura de trazas (parseo PICK_SCNL + captura por estacion), sin EW.
test_trace: test_trace.c csnhypodbp_trace.o
	$(CC) $(CFLAGS) -o test_trace test_trace.c csnhypodbp_trace.o -lm

# Motor IASP91 (reusa la lib de csnloc); necesita iasp91.tbl/.hed en el cwd.
test_tt: test_tt.c csnhypodbp_tt.o ../csnloc/libcsnloc_tt.a
	$(CC) $(CFLAGS) -o test_tt test_tt.c csnhypodbp_tt.o ../csnloc/libcsnloc_tt.a -lm -lpthread

# Tienda maestra (GLib): una fila por grupo, identidad por grupo (no por qid).
test_master_store: test_master_store.c csnhypodbp_master.o csnhypodbp_row.o
	$(CC) $(CFLAGS) -o test_master_store test_master_store.c csnhypodbp_master.o \
	      csnhypodbp_row.o $(shell pkg-config --libs glib-2.0 gobject-2.0 gio-2.0) -lm

# Transporte de TYPE_MAGNITUDE: el modulo origen viaja en origin_version.
test_mag_msg: test_mag_msg.c
	$(CC) $(CFLAGS) -o test_mag_msg test_mag_msg.c \
	      $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
	      $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a -lm

# Orden de canales de la pestaña Ondas (channelsOrder x componentsOrder).
test_station_order: test_station_order.c csnhypodbp_station.o
	$(CC) $(CFLAGS) -o test_station_order test_station_order.c csnhypodbp_station.o -lm

# Estado de la vista de ondas: zoom por dos clics, mapeos x<->tiempo, ticks, escala.
# Enlaza el parser de color de `common` (ewgui_config.o usa kom -> libew_mt.a).
test_view: test_view.c csnhypodbp_view.o ../common/src/ewgui_config.o
	$(CC) $(CFLAGS) -o test_view test_view.c csnhypodbp_view.o ../common/src/ewgui_config.o \
	      $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a -lm -lpthread

../common/src/ewgui_config.o:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4 src/ewgui_config.o

# Localizadores offline (locator <name> <bin> <cfg>).
test_locator: test_locator.c csnhypodbp_locator.o
	$(CC) $(CFLAGS) -o test_locator test_locator.c csnhypodbp_locator.o -lm

# Overlay local de picadas manuales.
test_manual: test_manual.c csnhypodbp_manual.o
	$(CC) $(CFLAGS) -o test_manual test_manual.c csnhypodbp_manual.o -lm

# Pipeline offline locate (partes puras) + persistencia manual.
test_locate: test_locate.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o
	$(CC) $(CFLAGS) -o test_locate test_locate.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o -lm

# Regresion de la pestana Ondas: la 1a descarga debe fijar oldest (span).
test_fetch_span: test_fetch_span.c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o test_fetch_span test_fetch_span.c ../common/libewgui_gtk4.a -lm

test: test_filter test_group test_parse_solution test_magstore test_trace test_tt \
      test_master_store test_mag_msg test_station_order test_locator test_manual test_locate \
      test_fetch_span test_view
	./test_filter
	./test_group
	./test_parse_solution
	./test_magstore
	./test_trace
	./test_tt
	./test_master_store
	./test_mag_msg
	./test_station_order
	./test_locator
	./test_manual
	./test_locate
	./test_fetch_span
	./test_view

clean:
	rm -f csnhypodbp test_filter test_group test_parse_solution test_magstore \
	      test_trace test_tt test_master_store test_mag_msg test_station_order \
	      test_locator test_manual test_locate test_fetch_span test_view *.o
