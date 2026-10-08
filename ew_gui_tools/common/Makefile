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
EW_VERSION ?= earthworm_8.0

# Toolkits: gio-2.0 siempre (ewgui_actions). La capa de vista usa GTK3 por
# defecto; con `make GUI_TOOLKIT=gtk4` se compila con GTK4 + libadwaita.
# Las capas puras no incluyen gtk (ver tests/check_no_gtk.sh).
GUI_TOOLKIT ?= gtk3
ifeq ($(GUI_TOOLKIT),gtk4)
GUI_PKG      = gtk4 libadwaita-1
VIEW_SRC     = src/ewgui_view_gtk4.c
else
GUI_PKG      = gtk+-3.0
VIEW_SRC     = src/ewgui_view_gtk3.c
endif
GUI_CFLAGS   = $(shell pkg-config --cflags $(GUI_PKG))
GUI_LIBS     = $(shell pkg-config --libs $(GUI_PKG))

CFLAGS     += -Wall -Wextra -O2 -g -Iinclude \
              -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT \
              $(shell pkg-config --cflags gio-2.0) $(GUI_CFLAGS)

# kom/tport/logit/GetType viven en libew_mt.a (ver csnloc/pickS/csnrv).
EW_LIB      = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
LDLIBS     += -lm -lpthread $(shell pkg-config --libs gio-2.0) $(GUI_LIBS) $(EW_LIB)

ifeq ($(GUI_TOOLKIT),gtk4)
LIB  = libewgui_gtk4.a
else
LIB  = libewgui.a
endif
SRC  = $(filter-out src/ewgui_view_gtk3.c src/ewgui_view_gtk4.c,$(wildcard src/*.c)) $(VIEW_SRC)
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
