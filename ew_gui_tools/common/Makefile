# Librería común de los módulos gráficos (libewgui.a).
#
# Uso:
#   source ../../ew8_unix.sh
#   make -C ew_gui_tools/common            # construye libewgui.a
#   make -C ew_gui_tools/common check      # tests headless + check_no_gtk
#
# Las capas core/dsp/wave/geo NO deben incluir gtk/gtk.h (tests/check_no_gtk.sh).

CC         ?= gcc
AR         ?= ar

EW_HOME    ?= $(abspath $(CURDIR)/../..)
EW_VERSION ?= ew_8.0

# Toolkits: gio-2.0 siempre (ewgui_actions). La capa de vista usa GTK3 por
# defecto; con `make GUI_TOOLKIT=gtk4` se compila con GTK4 + libadwaita.
# Las capas puras no incluyen gtk (ver tests/check_no_gtk.sh).
GUI_TOOLKIT ?= gtk3
ifeq ($(GUI_TOOLKIT),gtk4)
GUI_PKG        = gtk4 libadwaita-1
VIEW_SRC       = src/ewgui_view_gtk4.c
EXPORT_UI_SRC  = src/ewgui_export_ui_gtk4.c
OVERLAY_UI_SRC = src/ewgui_overlay_ui_gtk4.c
MENU_UI_SRC    = src/ewgui_menu_ui_gtk4.c
TABLE_UI_SRC   = src/ewgui_table_ui_gtk4.c
else
GUI_PKG        = gtk+-3.0
VIEW_SRC       = src/ewgui_view_gtk3.c
EXPORT_UI_SRC  =
OVERLAY_UI_SRC =
MENU_UI_SRC    =
TABLE_UI_SRC   =
endif
GUI_CFLAGS   = $(shell pkg-config --cflags $(GUI_PKG))
GUI_LIBS     = $(shell pkg-config --libs $(GUI_PKG))

LIBPQ_CFLAGS = $(shell pkg-config --cflags libpq)
LIBPQ_LIBS   = $(shell pkg-config --libs libpq)

CFLAGS     += -Wall -Wextra -O2 -g -Iinclude \
              -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
              $(shell pkg-config --cflags gio-2.0) $(GUI_CFLAGS) $(LIBPQ_CFLAGS)

# kom/tport/logit/GetType viven en libew_mt.a (ver csnloc/pickS/csnrv).
EW_LIB      = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
LDLIBS     += -lm -lpthread $(shell pkg-config --libs gio-2.0) $(GUI_LIBS) $(EW_LIB) $(LIBPQ_LIBS)

ifeq ($(GUI_TOOLKIT),gtk4)
LIB  = libewgui_gtk4.a
else
LIB  = libewgui.a
endif
SRC  = $(filter-out src/ewgui_view_gtk3.c src/ewgui_view_gtk4.c src/ewgui_export_ui_gtk4.c src/ewgui_overlay_ui_gtk4.c src/ewgui_menu_ui_gtk4.c src/ewgui_table_ui_gtk4.c,$(wildcard src/*.c)) $(VIEW_SRC) $(EXPORT_UI_SRC) $(OVERLAY_UI_SRC) $(MENU_UI_SRC) $(TABLE_UI_SRC)
OBJ  = $(SRC:.c=.o)

# Cada tests/test_*.c es un binario headless independiente.
TESTS = $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))

.PHONY: all check bench clean

all: $(LIB)

$(LIB): $(OBJ)
	$(AR) rcs $@ $(OBJ)

build/%: tests/%.c $(LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDLIBS)

check: $(LIB) $(TESTS)
	@echo "== check_no_gtk =="
	@bash tests/check_no_gtk.sh
	@echo "== check_map_image =="
	@bash tests/test_check_map_image.sh
	@echo "== check_menu_ui =="
	@bash tests/test_menu_ui.sh
	@echo "== check_arc_layout =="
	@bash tests/test_arc_layout.sh
	@echo "== check_table_ui =="
	@bash tests/test_table_ui.sh
	@echo "== check_string_ops =="
	@bash tests/test_string_ops.sh
	@echo "== check_schema_coverage =="
	@bash tests/test_schema_coverage.sh
	@echo "== check_lines =="
	@bash tests/test_check_lines.sh
	@echo "== check_build_deps =="
	@bash tests/test_build_deps.sh
	@echo "== check_launch_doc =="
	@bash tests/test_launch_doc.sh
	@echo "== check_timeout_gnu =="
	@bash tests/test_timeout_gnu.sh
	@echo "== check_install_bin =="
	@bash tests/test_install_bin.sh
	@echo "== check_install =="
	@bash tests/test_install_check.sh
	@echo "== check_ew_monitor =="
	@bash tests/test_ew_monitor.sh
	@echo "== check_ew_monitor_module =="
	@bash tests/test_ew_monitor_module.sh
	@echo "== check_agent_permissions =="
	@bash tests/test_agent_permissions.sh
	@echo "== check_diag =="
	@bash tests/test_diag.sh
	@echo "== make_overlays =="
	@python3 tests/test_make_overlays.py
	@rc=0; for t in $(TESTS); do \
	    echo "== $$t =="; \
	    $$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "OK: todos los tests pasaron"; \
	else echo "FALLO: hay tests que fallaron"; exit 1; fi

# Bench headless de la envolvente (criterio C6). No forma parte de `check`.
bench: $(LIB)
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/bench_wave tests/bench_wave.c $(LIB) $(LDLIBS)
	@build/bench_wave

clean:
	rm -rf build $(OBJ) $(LIB)

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
