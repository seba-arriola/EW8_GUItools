# Makefile para ew_controller (Panel grafico de control de startstop - GTK4 + libadwaita + Earthworm 8)

CC = gcc
GUI_PKG = gtk4 libadwaita-1

# Flags de compilacion para GTK4 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags $(GUI_PKG)) -I$(EW_HOME)/$(EW_VERSION)/include -I../common/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs $(GUI_PKG)) -lm -lpthread

OBJS = ew_controller_main.o ew_controller_gui.o ew_controller_row.o

# Libreria base nativa de Earthworm 8 (contiene logit, kom, transport, getutil,
# sema_ew, threads_ew, sleep_ew, time_ew, etc. usados por ew_controller)
EWLIBS = ../common/libewgui_gtk4.a \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: ew_controller

../common/libewgui_gtk4.a:
	$(MAKE) -C ../common GUI_TOOLKIT=gtk4

ew_controller: $(OBJS) ../common/libewgui_gtk4.a
	$(CC) $(CFLAGS) -o ew_controller $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp ew_controller $(EW_HOME)/$(EW_VERSION)/bin/

clean:
	rm -f ew_controller *.o
