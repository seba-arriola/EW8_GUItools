# csntvpQT - port Qt6 de csntvp (visor de trazas en vivo + picking manual P/S).
#
# Convive con csntvp (GTK4) en startstop_unix.d y tiene .d PROPIO
# (csntvpqt.d, MyModuleId MOD_CSNTVPQT 175): el eco de una picada se
# deduplica por (mod,inst,seq) en csntvp_data.c:371-381, asi que compartir
# modulo haria que una picada de este port pisara un slot local del GTK.
#
# Invariantes (las mismas que csnrvQT / ew_controllerQT, docs/testLibQT.md):
#   - NO enlaza ../common/libewgui_gtk4.a (arrastraria GTK): la parte pura de
#     common/ se compila in situ (cw_*.o) y la infra Qt vive en commonqt/.
#   - la capa C (c/) no conoce Qt; la C++ (ui/) solo incluye <Qt...>,
#     ewguiqt/*.h y c/tvq_capi.h.
#   - cairo vive SOLO en c/ (trazas y eje); ui/ no lo incluye.
#   - el parser de TYPE_PICK_SCNL NO se copia: se compila ../csntvp/csntvp_util.c
#     para que el formato no pueda derivar del GTK (csntvp_util.c es puro).
#   - compila con -MMD -MP y .dep (common/tests/test_build_deps.sh).
#   - instala con common/tools/install_bin.sh (atomico, siempre).
MOD_KIT = qt6
.DEFAULT_GOAL := all

CC      ?= gcc
CXX     ?= g++
BUILDDIR := build
# OJO: `TARGET =` con `=` simple (ew8_compile.sh lo deriva con sed, no `:=`).
TARGET = csntvpQT

# ---------------------------------------------------------------------------
# Deteccion de Qt6 (misma que commonqt, csnrvQT y ew_controllerQT)
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
C_SRC := tvq_config tvq_sta tvq_trace tvq_env tvq_pick tvq_emit tvq_ring \
         tvq_purge tvq_paint tvq_axis tvq_core tvq_logit

# Logica pura que se COMPILA desde el hermano GTK (no se copia).
C_SIBLING := csntvp_util

# Subconjunto PURO de common/ que necesita la capa C (sin GTK). Se compila
# in situ: enlazar libewgui_gtk4.a meteria GTK.
CW_PURE := ewgui_ring ewgui_dsp ewgui_wave

# Capa C++/Qt. Los .h con Q_OBJECT se mocan (UI_HDR); el resto no.
UI_SRC := main tvq_mainwindow
UI_HDR := tvq_mainwindow

OBJ_C   := $(addprefix $(BUILDDIR)/c_,$(C_SRC:=.o))
OBJ_CS  := $(addprefix $(BUILDDIR)/cs_,$(C_SIBLING:=.o))
OBJ_CW  := $(addprefix $(BUILDDIR)/cw_,$(CW_PURE:=.o))
OBJ_UI  := $(addprefix $(BUILDDIR)/ui_,$(UI_SRC:=.o))
OBJ_MOC := $(addprefix $(BUILDDIR)/moc_,$(UI_HDR:=.o))

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
EWINC    := $(EW_HOME)/$(EW_VERSION)/include
EWLIBDIR := $(EW_HOME)/$(EW_VERSION)/lib

CAIRO    := $(shell pkg-config --cflags --libs cairo 2>/dev/null)

CFLAGS   = -Wall -O2 -g -fPIC $(CAIRO) -I$(EWINC) -I../common/include \
           -I../csntvp -Ic -D_LINUX -D_REENTRANT -fno-stack-protector \
           -U_FORTIFY_SOURCE
CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -I../commonqt/include -Ic -Iui -Itests

MOCFLAGS = -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I../commonqt/include -Ic -Iui

# ORDEN: las libs van DESPUES de los objetos y de los .a que las usan; si no,
# el linker estatico no resuelve los simbolos de Qt de boot.o.
QTLIBS   = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core
EWLIBS   = ../commonqt/build/libewguiqt.a $(EWLIBDIR)/libew_mt.a
LDFLAGS  = $(EWLIBS) $(QTLIBS) $(CAIRO) -lm -lpthread

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

$(TARGET): qt_check $(OBJ_C) $(OBJ_CS) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(EWLIBS)
	$(CXX) -o $@ $(OBJ_C) $(OBJ_CS) $(OBJ_CW) $(OBJ_UI) $(OBJ_MOC) $(LDFLAGS)
	@chmod +x $@

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/c_%.o: c/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/cs_%.o: ../csntvp/%.c | $(BUILDDIR)
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
# C puro: no necesitan Qt ni DISPLAY. Es la red que valida el modulo.
TEST_C := test_config test_sta test_trace test_pick test_env test_core \
          test_paint test_norm test_guide test_clean
TEST_C_BINS := $(addprefix $(BUILDDIR)/,$(TEST_C))
PARAMS_DIR   := $(abspath ../../run_working_v8/params)

check: check-c check-scripts

check-c: $(TEST_C_BINS)
	@for t in $(TEST_C); do \
		echo "== $$t =="; \
		$(BUILDDIR)/$$t || exit 1; \
	done

# El .d del GTK no tiene --print-config: el script de contrato es propio.
check-scripts: $(TARGET)
	@echo "== test_config =="
	@BIN=./$(TARGET) PARAMS=$(PARAMS_DIR) sh tests/test_config.sh
	@echo "== test_ui_invariants =="
	@bash tests/test_ui_invariants.sh
	@echo "== test_no_gtk_link =="
	@bash tests/test_no_gtk_link.sh
	@echo "== test_ui =="
	@sh tests/test_ui.sh
	@echo "== test_integration =="
	@bash tests/test_integration.sh

TEST_LIBS = $(OBJ_C) $(OBJ_CS) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a $(CAIRO) -lm

$(BUILDDIR)/test_config: tests/test_config.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_sta: tests/test_sta.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_trace: tests/test_trace.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_pick: tests/test_pick.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_env: tests/test_env.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_core: tests/test_core.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_paint: tests/test_paint.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_norm: tests/test_norm.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_guide: tests/test_guide.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

$(BUILDDIR)/test_clean: tests/test_clean.c $(OBJ_C) $(OBJ_CS) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(TEST_LIBS)

test: check
test-live: $(TARGET)
	@sh tests/test_live.sh

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_C_BINS)

clean-deps:
	rm -f $(BUILDDIR)/*.dep

# Contrato de validacion de ew8_compile.sh (flags ANTES del .d).
VALIDATE_FLAGS = csntvpQT|--print-config csntvpqt.d
.PHONY: print-VALIDATE_FLAGS
print-VALIDATE_FLAGS:
	@printf '%b\n' '$(VALIDATE_FLAGS)'

-include $(wildcard $(BUILDDIR)/*.dep)
