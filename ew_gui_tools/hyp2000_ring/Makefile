# Makefile para hyp2000_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con HYPOINVERSE-2000.
# Enlaza el motor vendorizado en hypoinverse/ (libhypoinverse.a, gfortran).
# Autocontenido, como csnloc: no depende de src/ de EarthWorm.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

HYPOLIB = hypoinverse/libhypoinverse.a

TARGET = hyp2000_ring

all: $(TARGET)

$(HYPOLIB):
	$(MAKE) -C hypoinverse

$(TARGET): hyp2000_ring.o $(HYPOLIB)
	$(CC) $(CFLAGS) -o $(TARGET) hyp2000_ring.o $(HYPOLIB) $(EWLIBS) \
	      -lgfortran $(LIBS)
	cp $(TARGET) $(EW_HOME)/$(EW_VERSION)/bin/

clean:
	-$(MAKE) -C hypoinverse clean
	-rm -f *.o $(TARGET)
