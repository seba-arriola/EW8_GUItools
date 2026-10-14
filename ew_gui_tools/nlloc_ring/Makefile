# Makefile para nlloc_ring
#
# Modulo EarthWorm de anillo que refina hipocentros con NonLinLoc (NLLoc).
# Enlaza el motor vendorizado en nlloc/ (libnlloc.a). Autocontenido.

CFLAGS = -O2 -g -Wall -I$(EW_HOME)/$(EW_VERSION)/include \
         -I../common/include -D_LINUX -D_REENTRANT

LIBS   = -lm -lpthread

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

NLLLIB = nlloc/libnlloc.a

TARGET = nlloc_ring

all: $(TARGET) install

# Instalación en el bin de EarthWorm: atómica y SIEMPRE (aunque el binario esté
# corriendo o ya esté al día). Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)

$(NLLLIB):
	$(MAKE) -C nlloc

$(TARGET): nlloc_ring.o $(NLLLIB)
	$(CC) $(CFLAGS) -o $(TARGET) nlloc_ring.o $(NLLLIB) $(EWLIBS) $(LIBS)
	@# la instalación la hace el target `install` (common/tools/install_bin.sh)

clean:
	-$(MAKE) -C nlloc clean
	-rm -f *.o $(TARGET)
