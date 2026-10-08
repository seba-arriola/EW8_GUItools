# Makefile para nlloc_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con NonLinLoc (NLLoc).
# Enlaza el motor vendorizado en nlloc/ (libnlloc.a). Autocontenido.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

NLLLIB = nlloc/libnlloc.a

TARGET = nlloc_ring

all: $(TARGET)

$(NLLLIB):
	$(MAKE) -C nlloc

$(TARGET): nlloc_ring.o $(NLLLIB)
	$(CC) $(CFLAGS) -o $(TARGET) nlloc_ring.o $(NLLLIB) $(EWLIBS) $(LIBS)
	cp $(TARGET) $(EW_HOME)/$(EW_VERSION)/bin/

clean:
	-$(MAKE) -C nlloc clean
	-rm -f *.o $(TARGET)
