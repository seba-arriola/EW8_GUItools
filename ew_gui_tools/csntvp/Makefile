# Makefile para csntvp (GTK3 + Earthworm 8)

CC = gcc
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags gtk+-3.0) -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE

LIBS = $(shell pkg-config --libs gtk+-3.0) -lm -lpthread

OBJS = csntvp.o

# Dejamos que libew_mt.a maneje kom, logit, getutil, transport, etc.
EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

csntvp: $(OBJS)
	$(CC) $(CFLAGS) -o csntvp $(OBJS) $(EWLIBS) $(LIBS)
	cp csntvp $(EW_HOME)/$(EW_VERSION)/bin/

# Unit test for the ported aplicar_filtro_iir (standalone, no EW/GTK deps)
test: test_filter
	./test_filter

test_filter: test_filter.c
	$(CC) -O2 -o test_filter test_filter.c -lm

clean:
	rm -f *.o csntvp test_filter
