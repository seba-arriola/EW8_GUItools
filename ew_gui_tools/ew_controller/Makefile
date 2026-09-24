# Makefile para ew_controller (Panel grafico de control de startstop - GTK3 + Earthworm 8)

CC = gcc

# Flags de compilacion para GTK3 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags gtk+-3.0) -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lm -lpthread

OBJS = ew_controller.o

# Libreria base nativa de Earthworm 8 (contiene logit, kom, transport, getutil,
# sema_ew, threads_ew, sleep_ew, time_ew, etc. usados por ew_controller)
EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

all: ew_controller

ew_controller: $(OBJS)
	$(CC) $(CFLAGS) -o ew_controller $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp ew_controller $(EW_HOME)/$(EW_VERSION)/bin/

clean:
	rm -f ew_controller *.o
