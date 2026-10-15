# Makefile para csnhypodbp (Visor de Hipocentros GTK4 + libadwaita + Earthworm 8) - MODULAR

CC = gcc
GUI_PKG = gtk4 libadwaita-1

# Flags de compilacion para GTK4 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) $(shell pkg-config --cflags libpq) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -I../csnloc -I../wavestore -I../csnwphase -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) $(shell pkg-config --libs libpq) -lm -lpthread

# Librerias nativas de Earthworm 8
EWLIBS = ../common/libewgui_gtk4.a \
         ../csnwphase/libcsnwphase.a \
         ../csnloc/libcsnloc_tt.a \
         ../wavestore/libwavestore.a \
         $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

# Objetos modulares (el DSP vive ahora en libewgui)
OBJS = csnhypodbp_main.o csnhypodbp_gui.o csnhypodbp_ws.o csnhypodbp_row.o \
       csnhypodbp_group.o csnhypodbp_magchan.o csnhypodbp_trace.o csnhypodbp_tt.o csnhypodbp_master.o \
       csnhypodbp_station.o csnhypodbp_locator.o csnhypodbp_manual.o csnhypodbp_locate.o \
       csnhypodbp_view.o csnhypodbp_search.o csnhypodbp_sds.o csnhypodbp_statable.o \
       csnhypodbp_staitem.o csnhypodbp_preview.o csnhypodbp_profile.o \
       csnhypodbp_mapview.o csnhypodbp_magcfg.o csnhypodbp_magrow.o \
       csnhypodbp_magsta.o csnhypodbp_magitem.o \
        csnhypodbp_magmarks.o csnhypodbp_magview.o csnhypodbp_mww.o csnhypodbp_mwwerr.o

all: csnhypodbp install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: csnhypodbp
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin csnhypodbp

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

# Panel Mww: la GUI enlaza la MISMA libreria headless que el CLI csnwphase.
../csnwphase/libcsnwphase.a:
	$(MAKE) -C ../csnwphase libcsnwphase.a

../csnloc/libcsnloc_tt.a:
	$(MAKE) -C ../csnloc libcsnloc_tt.a

../wavestore/libwavestore.a:
	$(MAKE) -C ../wavestore libwavestore.a

csnhypodbp: $(OBJS) ../common/libewgui_gtk4.a ../csnwphase/libcsnwphase.a ../csnloc/libcsnloc_tt.a ../wavestore/libwavestore.a
	$(CC) $(CFLAGS) -o csnhypodbp $(OBJS) $(EWLIBS) $(LDFLAGS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

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

# Logica pura (solo stdio): no necesita GTK ni las librerias de EarthWorm.
test_mww_error: test_mww_error.c csnhypodbp_mwwerr.o
	$(CC) $(CFLAGS) -o test_mww_error test_mww_error.c csnhypodbp_mwwerr.o

../common/src/ewgui_config.o:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4 src/ewgui_config.o

# Localizadores offline (locator <name> <bin> <cfg>).
test_locator: test_locator.c csnhypodbp_locator.o
	$(CC) $(CFLAGS) -o test_locator test_locator.c csnhypodbp_locator.o -lm

# Overlay local de picadas manuales.
test_manual: test_manual.c csnhypodbp_manual.o
	$(CC) $(CFLAGS) -o test_manual test_manual.c csnhypodbp_manual.o -lm

# Pipeline offline locate (partes puras) + persistencia manual.
test_locate: test_locate.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o csnhypodbp_statable.o csnhypodbp_magmarks.o
	$(CC) $(CFLAGS) -o test_locate test_locate.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o csnhypodbp_statable.o csnhypodbp_magmarks.o -lm

# Arnes MANUAL de la cadena offline del boton `locate` (sin GTK). Corre
# picks -> csnloc -> refinador -> magnitud con los mismos parametros que la GUI
# y NO borra el directorio temporal. Necesita resources/ y los binarios
# instalados, asi que NO forma parte de `make test`:
#   make -C ew_gui_tools/csnhypodbp chain
#   ./locate_chain <arc_entrada> [refinador] [tmp_root]
chain: locate_chain
locate_chain: locate_chain.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o csnhypodbp_statable.o csnhypodbp_magmarks.o
	$(CC) $(CFLAGS) -o locate_chain locate_chain.c csnhypodbp_locate.o csnhypodbp_group.o csnhypodbp_locator.o csnhypodbp_statable.o csnhypodbp_magmarks.o -lm

# Tabla de estaciones: dedup M>A>T, residual de la solucion y color de traza.
test_statable: test_statable.c csnhypodbp_statable.o csnhypodbp_trace.o
	$(CC) $(CFLAGS) -o test_statable test_statable.c csnhypodbp_statable.o csnhypodbp_trace.o -lm

# Modelo de la tabla de estaciones (GObject): una fila por (estacion, fase).
test_staitem: test_staitem.c csnhypodbp_staitem.o
	$(CC) $(CFLAGS) -o test_staitem test_staitem.c csnhypodbp_staitem.o \
	      $(shell pkg-config --libs glib-2.0 gobject-2.0) -lm

# Regresion de la pestana Ondas: la 1a descarga debe fijar oldest (span).
test_fetch_span: test_fetch_span.c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o test_fetch_span test_fetch_span.c ../common/libewgui_gtk4.a -lm

# Nucleo puro del formulario de busqueda (parseo/validacion de rangos).
test_search: test_search.c csnhypodbp_search.o
	$(CC) $(CFLAGS) -o test_search test_search.c csnhypodbp_search.o -lm

# Tablita del preview de `locate`: titulos por estado y formato de los 7 valores.
test_preview: test_preview.c csnhypodbp_preview.o
	$(CC) $(CFLAGS) -o test_preview test_preview.c csnhypodbp_preview.o -lm

# Perfiles de profundidad: recta, distancia perpendicular, mas cercano y
# lector de polilineas de slab/topografia.
test_profile: test_profile.c csnhypodbp_profile.o
	$(CC) $(CFLAGS) -o test_profile test_profile.c csnhypodbp_profile.o -lm

# Geometria de los paneles de mapa: bbox, auto-encuadre y marco del perfil.
# Usa ewgui_geo_project (common, sin GTK).
test_mapview: test_mapview.c csnhypodbp_mapview.o ../common/src/ewgui_geo.o
	$(CC) $(CFLAGS) -o test_mapview test_mapview.c csnhypodbp_mapview.o \
	      ../common/src/ewgui_geo.o -lm

../common/src/ewgui_geo.o:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4 src/ewgui_geo.o

# Modelo de la fila de la tabla de magnitudes (GObject): una fila por estacion.
test_magitem: test_magitem.c csnhypodbp_magitem.o csnhypodbp_magrow.o
	$(CC) $(CFLAGS) -o test_magitem test_magitem.c csnhypodbp_magitem.o \
	      csnhypodbp_magrow.o $(shell pkg-config --libs glib-2.0 gobject-2.0) -lm

# Cache de las magnitudes por estacion que llegan vivas (anillo acotado).
test_magsta: test_magsta.c csnhypodbp_magsta.o
	$(CC) $(CFLAGS) -o test_magsta test_magsta.c csnhypodbp_magsta.o -lm

# Modelo de la tabla de magnitudes (una fila por estacion): NSLC, residual, celdas.
test_magrow: test_magrow.c csnhypodbp_magrow.o
	$(CC) $(CFLAGS) -o test_magrow test_magrow.c csnhypodbp_magrow.o -lm

test_magmarks: test_magmarks.c csnhypodbp_magmarks.o
	$(CC) $(CFLAGS) -o test_magmarks test_magmarks.c csnhypodbp_magmarks.o -lm

test_magview: test_magview.c csnhypodbp_magview.o csnhypodbp_magcfg.o ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o test_magview test_magview.c csnhypodbp_magview.o csnhypodbp_magcfg.o ../common/libewgui_gtk4.a -lm

# Parametros de magnitud que la GUI lee del .d de csnmags (defaults y ventanas).
test_magcfg: test_magcfg.c csnhypodbp_magcfg.o
	$(CC) $(CFLAGS) -o test_magcfg test_magcfg.c csnhypodbp_magcfg.o -lm

# Lector SDS (libmseed2): genera un miniSEED sintetico y lo lee de vuelta.
test_sds_read: test_sds_read.c csnhypodbp_sds.o ../common/src/ewgui_sds.o
	$(CC) $(CFLAGS) -o test_sds_read test_sds_read.c csnhypodbp_sds.o \
	      ../common/src/ewgui_sds.o $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a -lm

# Canales por estación de un TYPE_MAGNITUDE: rd_chan_mag NO acota sus escrituras
# contra el tamaño que se le pasa (solo lo usa para su memset inicial).
test_magchan: test_magchan.c csnhypodbp_magchan.o
	$(CC) $(CFLAGS) -o test_magchan test_magchan.c csnhypodbp_magchan.o \
	      $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
	      $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a -lm

test: test_filter test_group test_parse_solution test_magstore test_trace test_tt \
      test_master_store test_mag_msg test_magchan test_station_order test_locator test_manual test_locate \
      test_fetch_span test_view test_search test_sds_read test_statable test_staitem \
      test_preview test_profile test_mapview test_magcfg test_magrow test_magsta \
      test_magitem test_magmarks test_magview test_mww_error
	./test_filter
	./test_group
	./test_parse_solution
	./test_magstore
	./test_trace
	./test_tt
	./test_master_store
	./test_mag_msg
	./test_magchan
	./test_station_order
	./test_locator
	./test_manual
	./test_locate
	./test_fetch_span
	./test_view
	./test_search
	./test_sds_read
	./test_statable
	./test_staitem
	./test_preview
	./test_profile
	./test_mapview
	./test_magcfg
	./test_magrow
	./test_magsta
	./test_magitem
	./test_magmarks
	./test_magview
	./test_mww_error
	sh ./test_ui_invariants.sh
	sh ./test_config.sh

# El motor (rd_chan_mag) no acota sus escrituras y ademas lee 1 byte mas alla del
# mensaje: eso solo lo ve valgrind (ASan no instrumenta el codigo de EarthWorm).
# `test_parse_solution` va aqui porque `csnhypodbp_parse_solution` lee campos por
# offset y hay que comprobar que no se sale con mensajes cortos.
check-valgrind: test_magchan test_parse_solution
	valgrind --error-exitcode=1 --quiet ./test_magchan
	valgrind --error-exitcode=1 --quiet ./test_parse_solution

# Prueba LIVE del cierre al maximizar (necesita entorno, DISPLAY y EarthWorm
# arriba; se SALTA sola si falta cualquiera de los tres). Ver AGENTS §7.
test-live: csnhypodbp
	sh ./test_maximize.sh

clean:
	rm -f csnhypodbp test_filter test_group test_parse_solution test_magstore \
	      test_trace test_tt test_master_store test_mag_msg test_station_order \
	      test_locator test_manual test_locate test_fetch_span test_view test_search \
	      test_sds_read test_statable test_staitem test_preview test_profile \
	      test_mapview test_magcfg test_magrow test_magsta test_magitem \
	      test_magmarks test_magview test_mww_error \
	      locate_chain *.o

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
