# ew_controllerQT - port Qt6 del panel de control de startstop.
#
# Convive con ew_controller (GTK4): mismo CONTROL_RING, MyModuleId distinto
# (MOD_EW_CONTROLLERQT), .d propio. startstop contesta al modid que pregunta,
# asi que los dos pueden correr a la vez para comparar.
#
# Invariantes (las mismas que csnhypodbpQT, docs/testLibQT.md R1-R6):
#   - NO enlaza ../common/libewgui_gtk4.a (arrastraria GTK): la parte pura de
#     common/ se compila in situ (cw_*.o) y la infra Qt vive en commonqt/.
#   - la capa C (c/) no conoce Qt; la C++ (ui/) solo incluye <Qt...>,
#     ewguiqt/*.h y c/ecq_capi.h.
#   - compila con -MMD -MP y .dep (common/tests/test_build_deps.sh).
#   - instala con common/tools/install_bin.sh (atomico, siempre).
MOD_KIT = qt6
.DEFAULT_GOAL := all

CC      ?= gcc
CXX     ?= g++
BUILDDIR := build
# OJO: `TARGET =` con `=` simple: ew8_compile.sh deriva el binario con
# `sed 's/^[[:space:]]*TARGET[[:space:]]*=[[:space:]]*//'` (no entiende `:=`).
TARGET = ew_controllerQT

# ---------------------------------------------------------------------------
# Deteccion de Qt6 (misma que commonqt y csnhypodbpQT)
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
C_SRC := ecq_config ecq_core ecq_cfg ecq_log ecq_logit

# Subconjunto PURO de common/ que necesita la capa C (sin GTK). Se compila in
# situ, igual que csnhypodbpQT: enlazar libewgui_gtk4.a meteria GTK.
CW_PURE := ewgui_config ewgui_ctrl ewgui_cfgfile ewgui_ring

# Capa C++/Qt. Los .h con Q_OBJECT se mocan (UI_HDR); el resto no.
UI_SRC := main ecq_mainwindow
UI_HDR := ecq_mainwindow

OBJ_C   := $(addprefix $(BUILDDIR)/c_,$(C_SRC:=.o))
OBJ_CW  := $(addprefix $(BUILDDIR)/cw_,$(CW_PURE:=.o))
OBJ_UI  := $(addprefix $(BUILDDIR)/ui_,$(UI_SRC:=.o))
OBJ_MOC := $(addprefix $(BUILDDIR)/moc_,$(UI_HDR:=.o))

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
EWINC    := $(EW_HOME)/$(EW_VERSION)/include
EWLIBDIR := $(EW_HOME)/$(EW_VERSION)/lib

CFLAGS   = -Wall -O2 -g -fPIC -I$(EWINC) -I../common/include -Ic \
           -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE
CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -I../commonqt/include -Ic -Iui -Itests

MOCFLAGS = -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I../commonqt/include -Ic -Iui

# lib commonqt (Qt puro) + la parte de EarthWorm que necesita kom (config .d).
# ORDEN: las libs van DESPUES de los objetos y de los .a que las usan; si no,
# el linker estatico no resuelve los simbolos de Qt de boot.o.
QTLIBS   = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core
EWLIBS   = ../commonqt/build/libewguiqt.a $(EWLIBDIR)/libew_mt.a
LDFLAGS  = $(EWLIBS) $(QTLIBS) -lm -lpthread

# Dependencias de cabeceras (AGENTS §5).
CFLAGS   += -MMD -MP -MF $(BUILDDIR)/$(@F).dep
CXXFLAGS += -MMD -MP -MF $(BUILDDIR)/$(@F).dep

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all install qt_check check check-c test test-live test-parity clean clean-deps

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

# La lib compartida: se construye sola si un worktree nuevo no la tiene (el
# orden de ew8_compile.sh es alfabetico y commonqt va antes, pero `make` a secas
# en el modulo debe funcionar igual).
../commonqt/build/libewguiqt.a:
	$(MAKE) -C ../commonqt

# --- tests ---------------------------------------------------------------
# C puro: no necesitan Qt. Es la red que valida la logica del modulo.
TEST_C := test_config test_core test_cfg_editor test_log test_logit
TEST_C_BINS := $(addprefix $(BUILDDIR)/,$(TEST_C))
PARAMS_DIR   := $(abspath ../../run_working_v8/params)
FIXTURES_DIR := $(abspath ../ew_controller/tests/fixtures)

check: check-c check-scripts

check-c: $(TEST_C_BINS) $(BUILDDIR)/fake_status
	@for t in $(TEST_C); do \
		echo "== $$t =="; \
		$(BUILDDIR)/$$t || exit 1; \
	done

# fake_status se reutiliza del arnes del modulo GTK (misma fuente, no una copia).
$(BUILDDIR)/fake_status: ../ew_controller/tests/fake_status.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(EWLIBDIR)/libew_mt.a -lm

# Invariantes de fuente + arneses deterministas (offscreen). Necesitan el
# binario construido (no instalado).
check-scripts: $(TARGET)
	@echo "== test_ui_invariants =="
	@bash tests/test_ui_invariants.sh
	@echo "== test_no_gtk_link =="
	@bash tests/test_no_gtk_link.sh
	@echo "== test_layout =="
	@sh tests/test_layout.sh
	@echo "== test_parity =="
	@sh tests/test_parity.sh
	@echo "== test_integration =="
	@sh tests/test_integration.sh

$(BUILDDIR)/test_config: tests/test_config.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -DPARAMS_DIR='"$(PARAMS_DIR)"' -o $@ $< $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a -lm

$(BUILDDIR)/test_core: tests/test_core.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -DFIXTURES_DIR='"$(FIXTURES_DIR)"' -o $@ $< $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a -lm

$(BUILDDIR)/test_cfg_editor: tests/test_cfg_editor.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a -lm

$(BUILDDIR)/test_log: tests/test_log.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a -lm

$(BUILDDIR)/test_logit: tests/test_logit.c $(OBJ_C) $(OBJ_CW) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_C) $(OBJ_CW) $(EWLIBDIR)/libew_mt.a -lm

test: check
test-live: $(TARGET)
	@sh tests/test_live.sh
test-parity: check-scripts

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_C_BINS)

clean-deps:
	rm -f $(BUILDDIR)/*.dep

# Contrato de validacion de ew8_compile.sh (flags ANTES del .d).
VALIDATE_FLAGS = ew_controllerQT|--print-config ew_controllerqt.d
.PHONY: print-VALIDATE_FLAGS
print-VALIDATE_FLAGS:
	@printf '%b\n' '$(VALIDATE_FLAGS)'

-include $(wildcard $(BUILDDIR)/*.dep)
