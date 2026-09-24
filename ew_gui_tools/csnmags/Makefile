# Makefile para csnmags_toy

CFLAGS = -O -g -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = csnmags_toy.o csnmagsutils.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/ws_clientII.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew_common.o \
         $(EW_HOME)/$(EW_VERSION)/lib/socket_ew.o \
         $(EW_HOME)/$(EW_VERSION)/lib/rw_mag.o \
         $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

csnmags_toy: $(OBJS)
	$(CC) $(CFLAGS) -o csnmags_toy $(OBJS) $(EWLIBS) $(LIBS)
	cp csnmags_toy $(EW_HOME)/$(EW_VERSION)/bin/

clean:
	rm -f *.o csnmags_toy
