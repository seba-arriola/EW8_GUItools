# Makefile para hyp2000_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con HYPOINVERSE-2000.
# Enlaza el motor vendorizado en hypoinverse/ (libhypoinverse.a, gfortran).
# Autocontenido, como csnloc: no depende de src/ de EarthWorm.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -I../common/include -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

HYPOLIB = hypoinverse/libhypoinverse.a

TARGET = hyp2000_ring

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(HYPOLIB):
	$(MAKE) -C hypoinverse

$(TARGET): hyp2000_ring.o $(HYPOLIB)
	$(CC) $(CFLAGS) -o $(TARGET) hyp2000_ring.o $(HYPOLIB) $(EWLIBS) \
	      -lgfortran $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

clean:
	-$(MAKE) -C hypoinverse clean
	-rm -f *.o $(TARGET)
