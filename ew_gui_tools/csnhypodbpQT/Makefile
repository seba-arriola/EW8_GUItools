# csnhypodbpQT - prototipo Qt6 del GUI csnhypodbp.
#
# Port de csnhypodbp sin tocar el codigo existente: este Makefile compila IN SITU
# los .c puros de ../csnhypodbp/ y el subconjunto puro de ../common/src/, y
# reimplementa solo la capa de presentacion (ui/, C++/Qt6) y la fontaneria de
# datos (c/, C).
#
# Invariantes que este Makefile cumple (ver docs/testLibQT.md):
#   - NO enlaza ../common/libewgui_gtk4.a: el binario no lleva GTK/Adwaita/glib.
#   - compila con -MMD -MP y .dep (common/tests/test_build_deps.sh).
#   - "timeout" solo por ruta absoluta en los .sh (test_timeout_gnu.sh).
#   - instala con common/tools/install_bin.sh (atomico, siempre).
#
# MOD_KIT: qt6 - lo lee ew8_compile.sh (preflight de Qt + grupo GUI). Ya no
# hace falta el comentario-mentira `MOD_GUI: qt6`: la clasificacion es explicita.
MOD_KIT = qt6
.DEFAULT_GOAL := all

CC      ?= gcc
CXX     ?= g++
BUILDDIR := build
# OJO: `TARGET =` con `=` simple (no `:=`): ew8_compile.sh:328 deriva el binario
# con `sed 's/^[[:space:]]*TARGET[[:space:]]*=[[:space:]]*//'`, que NO reconoce
# `:=` y dejaria el modulo clasificado como libreria.
TARGET = csnhypodbpQT

# ---------------------------------------------------------------------------
# Deteccion de Qt6 (sin qmake6 en PATH se puede usar pkg-config como fallback).
# El error es DIFERIDO (target `qt_check`) para que `make check-c` y los tests
# headless de la capa C funcionen en una maquina sin Qt.
# ---------------------------------------------------------------------------
QMAKE := $(shell command -v qmake6 2>/dev/null)
ifneq ($(QMAKE),)
QT_INC  := $(shell $(QMAKE) -query QT_INSTALL_HEADERS)
QT_LIB  := $(shell $(QMAKE) -query QT_INSTALL_LIBS)
QT_ARCH := $(shell $(QMAKE) -query QT_INSTALL_ARCHDATA)
QT_MOC  := $(shell $(QMAKE) -query QT_HOST_LIBEXECS)/moc
else
QT_INC  := $(shell pkg-config --variable=includedir Qt6Widgets 2>/dev/null)
QT_LIB  := $(shell pkg-config --variable=libdir Qt6Widgets 2>/dev/null)
QT_ARCH := /usr/lib/qt6
QT_MOC  := /usr/lib/qt6/libexec/moc
endif
QT_HAVE := $(shell test -n "$(QT_INC)" && test -d "$(QT_INC)/QtWidgets" && test -x "$(QT_MOC)" && echo 1)

# ---------------------------------------------------------------------------
# Fuentes
# ---------------------------------------------------------------------------
# Capa C propia (fontaneria: config, anillos, BD, ondas, emision, lienzos).
C_SRC := bpq_config.c bpq_tt.c bpq_tt1d.c bpq_canvas.c bpq_rings.c bpq_emit.c bpq_magmsg.c bpq_events.c \
         bpq_map.c bpq_profile.c bpq_wave.c bpq_wavecache.c bpq_sds_globals.c bpq_stations.c \
         bpq_traces.c bpq_db.c bpq_locate.c bpq_manual.c bpq_mag.c \
         bpq_magpanel.c bpq_draw_mag.c bpq_axis.c bpq_mww.c bpq_draw_ball.c bpq_pin.c

# Nucleos PUROS de csnhypodbp que se compilan in situ (sin GTK; ver plan §4.1).
# Excluidos a proposito: row/staitem/magitem/master (GObject) y
# gui/main/ws/mww (GTK).
BP_PURE := group search statable preview profile mapview view trace traces \
           station filtercfg locate locator fixdepth magmarks magcfg magrow \
           magview magsta magchan manual tt sds mwwerr mww_over

# Subconjunto PURO de common (sin GTK; excluye ewgui_export.c y los *_gtk4.c).
CW_PURE := ewgui_wave ewgui_dsp ewgui_geo ewgui_sds ewgui_sta ewgui_config \
           ewgui_cfgfile ewgui_db ewgui_db_ids ewgui_ring ewgui_stalta \
           ewgui_table_cmp ewgui_overlay ewgui_version

# Capa C++/Qt (solo <Qt...> y bpq_capi.h). El lienzo vive en commonqt/ y aqui
# queda solo su adaptador (bpq_canvas_widget.h, sin .cxx).
UI_SRC  := main bpq_mainwindow bpq_events_model bpq_ring_worker \
           bpq_stations_model bpq_wave_worker bpq_search_dialog bpq_locate_worker \
           bpq_preview_panel bpq_filters_dialog bpq_mag_model bpq_mww_panel
UI_HDR  := bpq_mainwindow bpq_canvas_widget bpq_events_model bpq_ring_worker \
           bpq_stations_model bpq_wave_worker bpq_search_dialog bpq_locate_worker \
           bpq_preview_panel bpq_filters_dialog bpq_mag_model bpq_mww_panel

OBJ_C   := $(addprefix $(BUILDDIR)/c_,$(C_SRC:.c=.o))
OBJ_BP  := $(addprefix $(BUILDDIR)/bp_,$(BP_PURE:=.o))
OBJ_CW  := $(addprefix $(BUILDDIR)/cw_,$(CW_PURE:=.o))
OBJ_UI  := $(addprefix $(BUILDDIR)/ui_,$(UI_SRC:=.o))
OBJ_MOC := $(addprefix $(BUILDDIR)/moc_,$(UI_HDR:=.o))

# Solo lo que necesita el test de configuracion (no arrastra locate.c).
# ewgui_config.c aporta ewgui_parse_hex_color (usado por csnhypodbp_view.c).
CFG_PURE := filtercfg profile magcfg traces locator view trace mapview
OBJ_CFG  := $(addprefix $(BUILDDIR)/bp_,$(CFG_PURE:=.o)) \
            $(BUILDDIR)/cw_ewgui_geo.o $(BUILDDIR)/cw_ewgui_config.o

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
EWINC    := $(EW_HOME)/$(EW_VERSION)/include
EWLIBDIR := $(EW_HOME)/$(EW_VERSION)/lib

# cairo: necesario para los lienzos (c/), nunca expuesto a ui/.
CAIRO    := $(shell pkg-config --cflags --libs cairo 2>/dev/null)
PQCFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
PQLIBS   := $(shell pkg-config --libs libpq 2>/dev/null)

CFLAGS   = -Wall -O2 -g -fPIC $(CAIRO) $(PQCFLAGS) -I$(EWINC) \
           -I../common/include -I../csnhypodbp -I../csnloc -I../wavestore \
           -I../csnwphase -Ic \
           -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -Ic -Iui -Itests \
           -I../csnhypodbp -I../common/include -I../commonqt/include

MOCFLAGS = -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -Ic -Iui -I../csnhypodbp -I../common/include -I../commonqt/include

LDFLAGS  = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core $(CAIRO) $(PQLIBS) -lm -lpthread

# .a / .o ajenos. NO se enlaza libewgui_gtk4.a (arrastraria GTK).
# commonqt/build/libewguiqt.a es la infra Qt compartida (canvas, modelos, boot).
EWLIBS   = ../commonqt/build/libewguiqt.a \
           ../csnwphase/libcsnwphase.a ../csnloc/libcsnloc_tt.a \
           ../wavestore/libwavestore.a \
           $(EWLIBDIR)/libmseed2.a $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a

# Dependencias de cabeceras: sin esto la ABI se rompe en silencio (AGENTS §5).
CFLAGS   += -MMD -MP -MF $(BUILDDIR)/$(@F).dep
CXXFLAGS += -MMD -MP -MF $(BUILDDIR)/$(@F).dep

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
all: $(TARGET) install

install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(TARGET): qt_check $(OBJ_C) $(OBJ_BP) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(EWLIBS)
	$(CXX) -o $@ $(OBJ_C) $(OBJ_BP) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(EWLIBS) $(LDFLAGS)
	@chmod +x $@

# El link lo dirige g++ (inicializacion estatica de C++), pero ningun TU C++
# incluye headers de C/EW: la frontera es c/bpq_capi.h (regla R1 del plan).
qt_check:
	@if [ "$(QT_HAVE)" != "1" ]; then \
		echo "ERROR: faltan las herramientas de Qt6 (headers + moc)."; \
		echo "       sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0"; \
		exit 1; \
	fi

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# --- objetos de la capa C propia ---
$(BUILDDIR)/c_%.o: c/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- objetos de los nucleos puros de csnhypodbp (in situ, sin copiar) ---
$(BUILDDIR)/bp_%.o: ../csnhypodbp/csnhypodbp_%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- objetos del subconjunto puro de common (in situ) ---
$(BUILDDIR)/cw_%.o: ../common/src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- capa Qt ---
$(BUILDDIR)/ui_%.o: ui/%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/moc_%.cxx: ui/%.h | $(BUILDDIR)
	$(QT_MOC) $(MOCFLAGS) $< -o $@

$(BUILDDIR)/moc_%.o: $(BUILDDIR)/moc_%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- librerias ajenas (el orden de ew8_compile.sh es alfabetico: este modulo
#     va antes que csnloc/csnwphase/wavestore, asi que hay que construirlas) ---
../commonqt/build/libewguiqt.a:
	$(MAKE) -C ../commonqt
../csnloc/libcsnloc_tt.a:
	$(MAKE) -C ../csnloc libcsnloc_tt.a
../wavestore/libwavestore.a:
	$(MAKE) -C ../wavestore libwavestore.a
../csnwphase/libcsnwphase.a:
	$(MAKE) -C ../csnwphase libcsnwphase.a

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
TEST_C   := test_capi_config test_canvas test_rings test_events test_draw_map test_wave \
            test_wavecache test_tt1d test_tt1d_hyp test_stations test_traces test_db \
            test_locate test_preview test_manual test_manual_state test_magpanel \
            test_axis test_mww test_pin
TEST_CXX :=
PROBE_SRC := tests/probe_capi.cxx

# Test headless de la capa C: NO necesita Qt ni GTK. Se ejecuta con cwd =
# run_working_v8/params para que las rutas relativas del .d resuelvan como en
# produccion (igual que cuando lo lanza startstop).
PARAMS_DIR := $(abspath $(CURDIR)/../../run_working_v8/params)
THIS_DIR   := $(abspath $(CURDIR))

test_capi_config: tests/test_capi_config.c $(BUILDDIR)/c_bpq_config.o \
                  $(BUILDDIR)/c_bpq_sds_globals.o $(OBJ_CFG)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ \
	    tests/test_capi_config.c $(BUILDDIR)/c_bpq_config.o \
	    $(BUILDDIR)/c_bpq_sds_globals.o $(OBJ_CFG) \
	    $(EWLIBDIR)/libew_mt.a -lm -lpthread
	@chmod +x $@

# El lienzo despacha a mapa/perfil/trazas reales cuando hay contexto; las trazas
# arrastran la capa de ondas (EwGuiTrace) y los colores de picada (bp_trace).
CANVAS_DEPS = $(BUILDDIR)/c_bpq_canvas.o $(BUILDDIR)/c_bpq_map.o \
               $(BUILDDIR)/c_bpq_profile.o $(BUILDDIR)/c_bpq_traces.o \
               $(BUILDDIR)/c_bpq_draw_mag.o $(BUILDDIR)/c_bpq_axis.o \
               $(BUILDDIR)/c_bpq_wave.o \
               $(BUILDDIR)/c_bpq_sds_globals.o \
               $(BUILDDIR)/bp_mapview.o $(BUILDDIR)/bp_profile.o \
               $(BUILDDIR)/bp_view.o $(BUILDDIR)/bp_trace.o \
               $(BUILDDIR)/bp_traces.o \
               $(BUILDDIR)/bp_magview.o \
               $(BUILDDIR)/bp_magmarks.o \
               $(BUILDDIR)/c_bpq_draw_ball.o $(BUILDDIR)/c_bpq_mww.o \
               $(BUILDDIR)/bp_mww_over.o $(BUILDDIR)/bp_mwwerr.o $(DB_DEPS) \
               $(BUILDDIR)/cw_ewgui_geo.o $(BUILDDIR)/cw_ewgui_overlay.o \
               $(BUILDDIR)/cw_ewgui_config.o $(BUILDDIR)/cw_ewgui_wave.o \
               $(BUILDDIR)/cw_ewgui_dsp.o $(BUILDDIR)/cw_ewgui_stalta.o \
               $(BUILDDIR)/bp_sds.o $(BUILDDIR)/cw_ewgui_sds.o

test_canvas: tests/test_canvas.c $(CANVAS_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_canvas.c $(CANVAS_DEPS) \
	    $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# Anillos + mensajes: necesita rw_mag.o (wr_mag_scnl/rd_mag) y libew_mt.a (GetKey).
# Slots del nucleo que arrastran locate (confirmation_arc) y preview (mag_split).
RINGS_DEPS := $(BUILDDIR)/c_bpq_rings.o $(BUILDDIR)/c_bpq_emit.o \
              $(BUILDDIR)/c_bpq_magmsg.o $(BUILDDIR)/bp_magchan.o \
              $(BUILDDIR)/bp_group.o $(BUILDDIR)/bp_trace.o $(BUILDDIR)/bp_magrow.o \
              $(BUILDDIR)/bp_locate.o $(BUILDDIR)/bp_locator.o $(BUILDDIR)/bp_statable.o \
              $(BUILDDIR)/bp_magmarks.o $(BUILDDIR)/bp_fixdepth.o \
              $(BUILDDIR)/bp_preview.o $(BUILDDIR)/bp_search.o \
              $(BUILDDIR)/cw_ewgui_ring.o

test_rings: tests/test_rings.c $(RINGS_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_rings.c $(RINGS_DEPS) \
	    $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a -lm -lpthread
	@chmod +x $@

# Store de eventos: nucleos de agrupacion + formato UTC (search.c).
test_events: tests/test_events.c $(BUILDDIR)/c_bpq_events.o \
             $(BUILDDIR)/bp_group.o $(BUILDDIR)/bp_search.o $(BUILDDIR)/bp_locator.o
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_events.c \
	    $(BUILDDIR)/c_bpq_events.o $(BUILDDIR)/bp_group.o $(BUILDDIR)/bp_search.o \
	    $(BUILDDIR)/bp_locator.o -lm
	@chmod +x $@

# Historial/busqueda: nucleo de BD de common (libpq) + el store de eventos.
DB_DEPS := $(BUILDDIR)/c_bpq_db.o $(BUILDDIR)/c_bpq_events.o \
           $(BUILDDIR)/bp_group.o $(BUILDDIR)/bp_search.o $(BUILDDIR)/bp_locator.o \
           $(BUILDDIR)/bp_magrow.o $(BUILDDIR)/bp_magcfg.o \
           $(BUILDDIR)/cw_ewgui_db.o $(BUILDDIR)/cw_ewgui_db_ids.o

test_db: tests/test_db.c $(DB_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_db.c $(DB_DEPS) $(PQLIBS) \
	    -lm -lpthread
	@chmod +x $@

# Capa de ondas (fase 3): store + SDS + EwGuiTrace.
WAVE_DEPS := $(BUILDDIR)/c_bpq_wave.o $(BUILDDIR)/c_bpq_sds_globals.o \
             $(BUILDDIR)/bp_sds.o \
             $(BUILDDIR)/cw_ewgui_wave.o $(BUILDDIR)/cw_ewgui_sds.o \
             $(BUILDDIR)/cw_ewgui_dsp.o $(BUILDDIR)/cw_ewgui_stalta.o

test_wave: tests/test_wave.c $(WAVE_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_wave.c $(WAVE_DEPS) $(EWLIBS) \
	    $(CAIRO) -lm
	@chmod +x $@

# Cache de trazas (LRU por bytes): trazas sinteticas, sin store ni SDS.
test_wavecache: tests/test_wavecache.c $(WAVE_DEPS) \
                $(BUILDDIR)/c_bpq_wavecache.o $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_wavecache.c $(WAVE_DEPS) \
	    $(BUILDDIR)/c_bpq_wavecache.o $(EWLIBS) $(CAIRO) -lm
	@chmod +x $@

# Tiempos de viaje P en capas (.crh): formulas cerradas, sin store ni SDS.
test_tt1d: tests/test_tt1d.c $(BUILDDIR)/c_bpq_tt1d.o
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_tt1d.c \
	    $(BUILDDIR)/c_bpq_tt1d.o -lm
	@chmod +x $@

# Bandas de un .hyp (CRH/NOD/MUL): fixtures en /tmp + el .hyp real (cwd=params).
test_tt1d_hyp: tests/test_tt1d_hyp.c $(BUILDDIR)/c_bpq_tt1d.o
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_tt1d_hyp.c \
	    $(BUILDDIR)/c_bpq_tt1d.o -lm
	@chmod +x $@

# Catalogo de estaciones + tabla de picadas (statable es el nucleo puro).
STA_DEPS := $(BUILDDIR)/c_bpq_stations.o $(BUILDDIR)/c_bpq_tt.o \
            $(BUILDDIR)/c_bpq_tt1d.o \
            $(BUILDDIR)/bp_statable.o \
            $(BUILDDIR)/bp_station.o $(BUILDDIR)/bp_tt.o

test_stations: tests/test_stations.c $(STA_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_stations.c $(STA_DEPS) $(EWLIBS) \
	    -lm -lpthread
	@chmod +x $@

# Picadas efectivas del `locate` (M > A > Teo + MaxDistKm + SCNL del catalogo).
LOCATE_DEPS := $(BUILDDIR)/c_bpq_locate.o $(BUILDDIR)/c_bpq_stations.o \
               $(BUILDDIR)/c_bpq_tt.o $(BUILDDIR)/c_bpq_tt1d.o \
               $(BUILDDIR)/c_bpq_sds_globals.o $(BUILDDIR)/c_bpq_mag.o \
               $(BUILDDIR)/bp_locate.o $(BUILDDIR)/bp_locator.o \
               $(BUILDDIR)/bp_statable.o $(BUILDDIR)/bp_group.o \
               $(BUILDDIR)/bp_station.o \
               $(BUILDDIR)/bp_tt.o $(BUILDDIR)/bp_sds.o \
               $(BUILDDIR)/bp_magrow.o $(BUILDDIR)/bp_magcfg.o \
               $(BUILDDIR)/bp_magmarks.o \
               $(BUILDDIR)/cw_ewgui_sds.o

test_locate: tests/test_locate.c $(LOCATE_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_locate.c $(LOCATE_DEPS) \
	    $(EWLIBS) -lm -lpthread
	@chmod +x $@

# Núcleo puro del preview (estados, formateo y magnitud preferida).
PREVIEW_DEPS := $(BUILDDIR)/bp_preview.o $(BUILDDIR)/bp_search.o \
                $(BUILDDIR)/bp_filtercfg.o

test_preview: tests/test_preview.c $(PREVIEW_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_preview.c $(PREVIEW_DEPS) \
	    -lm
	@chmod +x $@

# Overlay de picadas manuales: set por fase, borrado y persistencia.
MANUAL_DEPS := $(BUILDDIR)/c_bpq_manual.o $(BUILDDIR)/bp_manual.o \
               $(BUILDDIR)/c_bpq_stations.o $(BUILDDIR)/c_bpq_tt.o \
               $(BUILDDIR)/c_bpq_tt1d.o \
               $(BUILDDIR)/bp_station.o \
               $(BUILDDIR)/bp_statable.o $(BUILDDIR)/bp_tt.o

test_manual: tests/test_manual.c $(MANUAL_DEPS) $(CANVAS_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_manual.c $(MANUAL_DEPS) \
	    $(CANVAS_DEPS) $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# Estado manual por evento: clave, migracion y no borrar al cambiar de solucion.
test_manual_state: tests/test_manual_state.c $(BUILDDIR)/c_bpq_manual.o \
                   $(BUILDDIR)/bp_manual.o
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_manual_state.c \
	    $(BUILDDIR)/c_bpq_manual.o $(BUILDDIR)/bp_manual.o -lm
	@chmod +x $@

# Canvas de trazas: reusa el juego completo del lienzo (mapa/perfil/trazas).
test_traces: tests/test_traces.c $(CANVAS_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_traces.c $(CANVAS_DEPS) \
	    $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# Sub-pestana magnitudes (6.1): ventanas (magmarks), filas (magrow), puerta
# (magcfg), overrides y lienzo de la traza procesada (magview) + franja.
MAG_DEPS := $(BUILDDIR)/c_bpq_magpanel.o $(BUILDDIR)/c_bpq_draw_mag.o \
            $(BUILDDIR)/c_bpq_axis.o \
            $(BUILDDIR)/c_bpq_wave.o $(BUILDDIR)/c_bpq_sds_globals.o \
            $(BUILDDIR)/bp_magmarks.o $(BUILDDIR)/bp_magcfg.o \
            $(BUILDDIR)/bp_statable.o \
            $(BUILDDIR)/bp_magrow.o $(BUILDDIR)/bp_magview.o $(BUILDDIR)/bp_sds.o \
            $(BUILDDIR)/cw_ewgui_wave.o $(BUILDDIR)/cw_ewgui_sds.o \
            $(BUILDDIR)/cw_ewgui_dsp.o $(BUILDDIR)/cw_ewgui_stalta.o

test_magpanel: tests/test_magpanel.c $(MAG_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_magpanel.c $(MAG_DEPS) \
	    $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# Eje de tiempo comun: estilo, paso 1-2-5, banda de arrastre y gesto de release.
test_axis: tests/test_axis.c $(CANVAS_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_axis.c $(CANVAS_DEPS) \
	    $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# Panel Mww (6.3): config de csnwphase + overrides de la tabla + playera.
MWW_DEPS := $(BUILDDIR)/c_bpq_mww.o $(BUILDDIR)/c_bpq_draw_ball.o \
            $(BUILDDIR)/bp_mww_over.o $(BUILDDIR)/bp_mwwerr.o $(DB_DEPS) \
            $(BUILDDIR)/cw_ewgui_sds.o $(BUILDDIR)/cw_ewgui_wave.o \
            $(BUILDDIR)/cw_ewgui_dsp.o $(BUILDDIR)/bp_sds.o \
            $(BUILDDIR)/c_bpq_sds_globals.o

test_mww: tests/test_mww.c $(MWW_DEPS) $(EWLIBS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_mww.c $(MWW_DEPS) $(EWLIBS) \
	    $(PQLIBS) $(CAIRO) -lm -lpthread
	@chmod +x $@

# Origen fijado (fase 7): nucleo puro del pin.
test_pin: tests/test_pin.c $(BUILDDIR)/c_bpq_pin.o
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_pin.c $(BUILDDIR)/c_bpq_pin.o -lm
	@chmod +x $@

# Fixture de ARCs sinteticos (tests del GUI y demos sin EarthWorm).
mk_test_arc: tests/mk_test_arc.c $(RINGS_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/mk_test_arc.c $(RINGS_DEPS) \
	    $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a -lm -lpthread
	@chmod +x $@

# Paneles de planta y perfil con contexto real.
test_draw_map: tests/test_draw_map.c $(CANVAS_DEPS)
	rm -f $@
	$(CC) $(CFLAGS) -Itests -o $@ tests/test_draw_map.c $(CANVAS_DEPS) \
	    $(EWLIBS) $(PQLIBS) $(CAIRO) -lm
	@chmod +x $@

# AC3: bpq_capi.h es C++-safe incluso incluido DESPUES de Qt (`slots`).
.PHONY: probe
probe: qt_check
	$(CXX) $(CXXFLAGS) -fsyntax-only $(PROBE_SRC)

# AC2/AC6: invariantes del binario (no necesitan DISPLAY).
.PHONY: check-scripts
check-scripts: $(TARGET)
	@sh tests/test_no_gtk_link.sh
	@sh tests/test_printconfig_parity.sh
	@sh tests/test_printconfig_parity.sh csnhypodbpqt.d
	@sh tests/test_integration.sh
	@sh tests/test_map_background.sh
	@sh tests/test_pin_invariants.sh
	@sh tests/test_traces_invariants.sh
	@sh tests/test_table_ui_invariants.sh
	@sh tests/test_ui_input.sh
	@sh tests/test_magdrag.sh
	@sh tests/test_pin.sh
	@sh tests/test_search_lock.sh

check-c: $(TEST_C)
	@chmod +x $(TEST_C)
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_capi_config csnhypodbp.d
	@$(THIS_DIR)/test_canvas
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_rings
	@$(THIS_DIR)/test_events
	@$(THIS_DIR)/test_draw_map
	@$(THIS_DIR)/test_wave
	@$(THIS_DIR)/test_wavecache
	@$(THIS_DIR)/test_tt1d
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_tt1d_hyp
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_stations
	@$(THIS_DIR)/test_traces
	@$(THIS_DIR)/test_db
	@$(THIS_DIR)/test_locate
	@$(THIS_DIR)/test_preview
	@$(THIS_DIR)/test_manual
	@$(THIS_DIR)/test_manual_state
	@$(THIS_DIR)/test_magpanel
	@$(THIS_DIR)/test_axis
	@$(THIS_DIR)/test_mww
	@$(THIS_DIR)/test_pin

check: qt_check $(TEST_C)
	@chmod +x $(TEST_C)
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_capi_config csnhypodbp.d
	@$(THIS_DIR)/test_canvas
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_rings
	@$(THIS_DIR)/test_events
	@$(THIS_DIR)/test_draw_map
	@$(THIS_DIR)/test_wave
	@$(THIS_DIR)/test_wavecache
	@$(THIS_DIR)/test_tt1d
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_tt1d_hyp
	@cd $(PARAMS_DIR) && $(THIS_DIR)/test_stations
	@$(THIS_DIR)/test_traces
	@$(THIS_DIR)/test_db
	@$(THIS_DIR)/test_locate
	@$(THIS_DIR)/test_preview
	@$(THIS_DIR)/test_manual
	@$(THIS_DIR)/test_manual_state
	@$(THIS_DIR)/test_magpanel
	@$(THIS_DIR)/test_axis
	@$(THIS_DIR)/test_mww
	@$(THIS_DIR)/test_pin
	@$(MAKE) --no-print-directory probe
	@$(MAKE) --no-print-directory check-scripts

test: check

test-live: $(TARGET)
	@if [ -x tests/test_maximize.sh ]; then sh tests/test_maximize.sh; \
	 elif [ -x tests/test_live_events.sh ]; then sh tests/test_live_events.sh; \
	 else echo "SKIP test-live: aun no hay tests/test_maximize.sh"; fi

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_C) $(TEST_CXX)

clean-deps:
	rm -f $(BUILDDIR)/*.dep

# Contrato de validacion de ew8_compile.sh (flags ANTES del .d, como csnhypodbp).
# Se validan los DOS .d: el del modulo GTK (paridad de esquema) y el propio del
# port (csnhypodbpqt.d, el que declara MOD_CSNHYPODBPQT y usa startstop).
# OJO: `printf '%b'` (no '%s'): el separador es un `\n` LITERAL dentro del valor.
VALIDATE_FLAGS = csnhypodbpQT|--print-config csnhypodbp.d\ncsnhypodbpQT|--check-profiles csnhypodbp.d\ncsnhypodbpQT|--print-config csnhypodbpqt.d\ncsnhypodbpQT|--check-profiles csnhypodbpqt.d
.PHONY: print-VALIDATE_FLAGS
print-VALIDATE_FLAGS:
	@printf '%b\n' '$(VALIDATE_FLAGS)'

-include $(wildcard $(BUILDDIR)/*.dep)
