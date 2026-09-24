# Makefile para csnhypodbp (Visor de Hipocentros GTK3 + Earthworm 8) - MODULAR

CC = gcc

# Flags de compilacion para GTK3 y Earthworm
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags gtk+-3.0) -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

# Librerias y enlazador
LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lm -lpthread

# Librerias nativas de Earthworm 8
EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

# Objetos modulares
OBJS = csnhypodbp_main.o csnhypodbp_gui.o csnhypodbp_ws.o csnhypodbp_dsp.o

all: csnhypodbp

csnhypodbp: $(OBJS)
	$(CC) $(CFLAGS) -o csnhypodbp $(OBJS) $(EWLIBS) $(LDFLAGS)
	cp csnhypodbp $(EW_HOME)/$(EW_VERSION)/bin/

# Test unitario del filtro IIR y manejo de gaps (DSP puro, sin GTK/EW link)
test_filter: csnhypodbp_dsp.o test_filter.c
	$(CC) $(CFLAGS) -o test_filter test_filter.c csnhypodbp_dsp.o -lm

test: test_filter
	./test_filter

clean:
	rm -f csnhypodbp test_filter *.o
