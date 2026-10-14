# Makefile para csnstaevdisp (Visor de Estaciones y Eventos - GTK3 + Earthworm 8)
# Carga de estaciones y de la historia de sismos propia (sin libsrc/ATWC).

MODULE = csnstaevdisp

CC = gcc

# Módulo migrado a GTK4 + libadwaita (Fase 6). Enlaza libewgui_gtk4.a.
GUI_PKG = gtk4 libadwaita-1

# Flags GTK4 + Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) $(shell pkg-config --cflags libpq) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -fcommon -D_LINUX -D_REENTRANT -D_INTEL -Wno-format

# Librerias base de Earthworm necesarias (objetos ya compilados)
EW_SRC_UTIL = $(EW_HOME)/$(EW_VERSION)/src/libsrc/util
EW_SRC_UNIX = $(EW_HOME)/$(EW_VERSION)/src/libsrc/unix

EW_OBJS = $(EW_SRC_UNIX)/transport.o \
          $(EW_SRC_UTIL)/getutil.o \
          $(EW_SRC_UTIL)/kom.o \
          $(EW_SRC_UNIX)/sleep_ew.o \
          $(EW_SRC_UNIX)/time_ew.o \
          $(EW_SRC_UTIL)/logit_mt.o \
          $(EW_SRC_UNIX)/sema_ew.o \
          $(EW_SRC_UNIX)/threads_ew.o \
          $(EW_SRC_UTIL)/swap.c

LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) $(shell pkg-config --libs libpq) -lm -lpthread

all: $(MODULE) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(MODULE)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(MODULE)

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

$(MODULE): $(MODULE).c ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o $(MODULE) $(MODULE).c ../common/libewgui_gtk4.a $(EW_OBJS) $(LDFLAGS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)
	@echo "======================================================"
	@echo " EXITO: $(MODULE) compilado y copiado a EW bin!"
	@echo "======================================================"

clean:
	rm -f $(MODULE) *.o test/test_data test/csnstaevdisp_nomain.o

# Verifica los parsers de estaciones e historia sin abrir la GUI.
PARAMS = ../../run_working_v8/params
test-data: test/test_data
	./test/test_data $(PARAMS)/estaciones_107.txt

# Verifica el parseo de config/overlays en modo headless (sin DISPLAY).
test-config: $(MODULE)
	BIN=./$(MODULE) bash ./test_config.sh

test/test_data: test/test_data.c $(MODULE).c
	$(CC) $(CFLAGS) -Dmain=csnstaevdisp_unused_main -c $(MODULE).c \
	      -o test/csnstaevdisp_nomain.o
	$(CC) $(CFLAGS) -o $@ test/test_data.c test/csnstaevdisp_nomain.o \
	      ../common/libewgui_gtk4.a $(EW_OBJS) $(LDFLAGS)

.PHONY: all clean test-data test-config
