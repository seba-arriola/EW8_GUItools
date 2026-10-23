# csnrvQT - port Qt6 de csnrv (visor regional / Report Viewer).
#
# Sustituye a csnrv (GTK4) en startstop_unix.d: corre con el MISMO csnrv.d
# (MOD_CSNRV 159) y su Process en startstop_unix.d; el del GTK esta retirado.
# Los dos son LECTORES del anillo HYPO_RING_REF, asi que pueden convivir si se
# vuelve a declarar el Process del GTK.
#
# Invariantes (las mismas que ew_controllerQT, docs/testLibQT.md R1-R6):
#   - NO enlaza ../common/libewgui_gtk4.a (arrastraria GTK): la parte pura de
#     common/ se compila in situ (cw_*.o) y la infra Qt vive en commonqt/.
#   - la capa C (c/) no conoce Qt; la C++ (ui/) solo incluye <Qt...>,
#     ewguiqt/*.h y c/crv_capi.h.
#   - cairo vive SOLO en c/ (mapa/export); ui/ no lo incluye.
#   - compila con -MMD -MP y .dep (common/tests/test_build_deps.sh).
#   - instala con common/tools/install_bin.sh (atomico, siempre).
MOD_KIT = qt6
.DEFAULT_GOAL := all

CC      ?= gcc
CXX     ?= g++
BUILDDIR := build
# OJO: `TARGET =` con `=` simple (ew8_compile.sh lo deriva con sed, no `:=`).
TARGET = csnrvQT

# ---------------------------------------------------------------------------
# Deteccion de Qt6 (misma que commonqt y ew_controllerQT)
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
# Capa C propia (sin Qt).
C_SRC := crv_config crv_arc crv_magmsg crv_mag crv_fmt crv_db crv_core crv_map crv_logit

# Subconjunto PURO de common/ que necesita la capa C (sin GTK). Se compila in
# situ: enlazar libewgui_gtk4.a meteria GTK.
CW_PURE := ewgui_config ewgui_ring ewgui_db ewgui_db_ids ewgui_geo ewgui_overlay

# Capa C++/Qt. Los .h con Q_OBJECT se mocan (UI_HDR); el resto no.
UI_SRC := main crv_mainwindow crv_overlay_dialog
UI_HDR := crv_mainwindow

OBJ_C   := $(addprefix $(BUILDDIR)/c_,$(C_SRC:=.o))
OBJ_CW  := $(addprefix $(BUILDDIR)/cw_,$(CW_PURE:=.o))
OBJ_UI  := $(addprefix $(BUILDDIR)/ui_,$(UI_SRC:=.o))
OBJ_MOC := $(addprefix $(BUILDDIR)/moc_,$(UI_HDR:=.o))

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
EWINC    := $(EW_HOME)/$(EW_VERSION)/include
EWLIBDIR := $(EW_HOME)/$(EW_VERSION)/lib

CAIRO    := $(shell pkg-config --cflags --libs cairo 2>/dev/null)
PQCFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
PQLIBS   := $(shell pkg-config --libs libpq 2>/dev/null)

CFLAGS   = -Wall -O2 -g -fPIC $(CAIRO) $(PQCFLAGS) -I$(EWINC) -I../common/include -Ic \
           -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE
CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -I../commonqt/include -Ic -Iui -Itests

MOCFLAGS = -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I../commonqt/include -Ic -Iui

# ORDEN: las libs van DESPUES de los objetos y de los .a que las usan; si no,
# el linker estatico no resuelve los simbolos de Qt de boot.o.
QTLIBS   = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core
EWLIBS   = ../commonqt/build/libewguiqt.a $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a
LDFLAGS  = $(EWLIBS) $(QTLIBS) $(CAIRO) $(PQLIBS) -lm -lpthread

# Dependencias de cabeceras (AGENTS §5).
CFLAGS   += -MMD -MP -MF $(BUILDDIR)/$(@F).dep
CXXFLAGS += -MMD -MP -MF $(BUILDDIR)/$(@F).dep

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all install qt_check check check-c check-scripts test test-live clean clean-deps

all: $(TARGET) install

install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

qt_check:
	@if [ "$(QT_HAVE)" != "1" ]; then \
		echo "ERROR: faltan las herramientas de Qt6 (headers + moc)."; \
		echo "       sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0"; \
		exit 1; \
	fi

$(TARGET): qt_check $(OBJ_C) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(EWLIBS)
	$(CXX) -o $@ $(OBJ_C) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(LDFLAGS)
	@chmod +x $@

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/c_%.o: c/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/cw_%.o: ../common/src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/ui_%.o: ui/%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/moc_%.cxx: ui/%.h | $(BUILDDIR)
	$(QT_MOC) $(MOCFLAGS) $< -o $@

$(BUILDDIR)/moc_%.o: $(BUILDDIR)/moc_%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

../commonqt/build/libewguiqt.a:
	$(MAKE) -C ../commonqt

# --- tests ---------------------------------------------------------------
# C puro: no necesitan Qt. Es la red que valida la logica del modulo.
TEST_C := test_config test_arc test_magmsg test_mag test_state test_map test_logit
TEST_C_BINS := $(addprefix $(BUILDDIR)/,$(TEST_C))
PARAMS_DIR   := $(abspath ../../run_working_v8/params)

check: check-c check-scripts

check-c: $(TEST_C_BINS)
	@for t in $(TEST_C); do \
		echo "== $$t =="; \
		$(BUILDDIR)/$$t || exit 1; \
	done

# El script del GTK se reutiliza SIN tocarlo: es el contrato de paridad de la
# configuracion (9 casos). Solo se cambia el binario.
check-scripts: $(TARGET)
	@echo "== test_headless =="
	@BIN=./$(TARGET) bash ../csnrv/test_config.sh
	@echo "== test_ui_invariants =="
	@bash tests/test_ui_invariants.sh
	@echo "== test_no_gtk_link =="
	@bash tests/test_no_gtk_link.sh
	@echo "== test_ui =="
	@sh tests/test_ui.sh
	@echo "== test_integration =="
	@sh tests/test_integration.sh

# `rd_mag` vive en rw_mag.o (NO en libew_mt.a): todos los tests enlazan el
# OBJ_C completo, que incluye crv_magmsg/crv_core, asi que lo necesitan todos.
TEST_LIBS = $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a \
            $(PQLIBS) $(CAIRO) -lm

$(BUILDDIR)/test_config: tests/test_config.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_arc: tests/test_arc.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_magmsg: tests/test_magmsg.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_mag: tests/test_mag.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_state: tests/test_state.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_map: tests/test_map.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_logit: tests/test_logit.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

test: check
test-live: $(TARGET)
	@sh tests/test_live.sh

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_C_BINS)

clean-deps:
	rm -f $(BUILDDIR)/*.dep

# Contrato de validacion de ew8_compile.sh (flags ANTES del .d).
VALIDATE_FLAGS = csnrvQT|--print-config csnrv.d\ncsnrvQT|--check-overlays csnrv.d
.PHONY: print-VALIDATE_FLAGS
print-VALIDATE_FLAGS:
	@printf '%b\n' '$(VALIDATE_FLAGS)'

-include $(wildcard $(BUILDDIR)/*.dep)
