# commonqt - infraestructura Qt6 compartida por los modulos GUI (libewguiqt.a).
#
# Hermana de common/ (que es GTK4 + libadwaita): no se mezclan, porque
# libewgui_gtk4.a arrastra GTK y cualquier modulo Qt quedaria enlazado a GTK.
# Esta lib depende SOLO de Qt6Core/Gui/Widgets: ni cairo, ni EarthWorm, ni
# glib/gtk.
#
# Reglas (docs/testLibQT.md R1/R2/R5, docs/QT-COMMON.md):
#   - include/ewguiqt/*.h solo declara Qt (+ std::function): cero cairo/EW/GTK.
#   - src/*.cxx solo incluye <Qt...> + ewguiqt/*.h (+ ewgui/table_ui.h, que es C
#     puro: de ahi sale el comparador compartido ewgui_table_cmp_values).
#   - el canvas entrega un buffer BGRA: quien crea la superficie cairo es la
#     capa C del modulo que lo usa (asi esta lib no enlaza cairo).
#
# La deteccion de Qt6 es la misma que csnhypodbpQT/Makefile: qmake6 y, si no
# esta en PATH, pkg-config. El error es DIFERIDO al target `qt_check` para que
# las herramientas de inventario (ew8_compile.sh --list) funcionen sin Qt.
MOD_KIT = qt6
.DEFAULT_GOAL := all

CXX     ?= g++
CC      ?= gcc
AR      ?= ar
BUILDDIR := build
LIB      = $(BUILDDIR)/libewguiqt.a

# ---------------------------------------------------------------------------
# Deteccion de Qt6
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
# Utilitaria C pura de common/ (comparador de celdas). Se compila in situ: no
# se enlaza libewgui_gtk4.a (arrastraria GTK).
CW_PURE := ewgui_table_cmp

# Modulos con Q_OBJECT (necesitan moc) y sin el.
SRC_MOC := table list_select canvas
SRC     := boot confirm theme log_view

OBJ     := $(addprefix $(BUILDDIR)/,$(SRC:=.o)) \
           $(addprefix $(BUILDDIR)/,$(SRC_MOC:=.o)) \
           $(addprefix $(BUILDDIR)/moc_,$(SRC_MOC:=.o)) \
           $(addprefix $(BUILDDIR)/cw_,$(CW_PURE:=.o))
OBJ_MOC_SRC := $(addprefix $(BUILDDIR)/moc_,$(SRC_MOC:=.cxx))

TESTS     := test_table test_boot test_widgets test_canvas
TEST_BINS := $(addprefix $(BUILDDIR)/,$(TESTS))

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -Iinclude -I../common/include
CFLAGS   = -Wall -O2 -g -fPIC -I../common/include

MOCFLAGS = -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -Iinclude -I../common/include

LDLIBS   = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core -lm -lpthread

# Dependencias de cabeceras: cambiar un .h recompila solo lo que lo incluye
# (AGENTS §5; common/tests/test_build_deps.sh).
CXXFLAGS += -MMD -MP -MF $(BUILDDIR)/$(@F).dep
CFLAGS   += -MMD -MP -MF $(BUILDDIR)/$(@F).dep

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all check clean qt_check

all: $(LIB)

qt_check:
	@if [ "$(QT_HAVE)" != "1" ]; then \
		echo "ERROR: faltan las herramientas de Qt6 (headers + moc)."; \
		echo "       sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0"; \
		exit 1; \
	fi

$(LIB): qt_check $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $(OBJ)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: src/%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/cw_%.o: ../common/src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/moc_%.cxx: include/ewguiqt/%.h | $(BUILDDIR)
	$(QT_MOC) $(MOCFLAGS) $< -o $@

$(BUILDDIR)/moc_%.o: $(BUILDDIR)/moc_%.cxx | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/test_%: tests/test_%.cxx $(LIB) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -Itests -o $@ $< $(LIB) $(LDLIBS)

# Los tests corren con el plugin `offscreen`: no necesitan DISPLAY (y no deben
# depender de WSLg). `LC_NUMERIC` lo fija boot(), pero los tests que no pasan
# por el tampoco deben depender del locale del shell.
check: qt_check $(LIB) $(TEST_BINS)
	@for t in $(TESTS); do \
		echo "== $$t =="; \
		LC_ALL=C QT_QPA_PLATFORM=offscreen $(BUILDDIR)/$$t || exit 1; \
	done
	@echo "== check_no_sci_deps =="
	@bash tests/test_no_sci_deps.sh
	@echo "== check_no_gtk_link =="
	@bash tests/test_no_gtk_link.sh

clean:
	rm -rf $(BUILDDIR)
