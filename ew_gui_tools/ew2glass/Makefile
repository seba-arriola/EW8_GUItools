#
#    Makefile for ew2glass
#

CC = gcc

# Directorios de librerías externas (Debes ajustarlos si están en otro lugar)
KAFKADIR = $(EW_HOME)/$(EW_VERSION)/src/data_exchange/ew2openapi/librdkafka/build
JSONCDIR = $(EW_HOME)/$(EW_VERSION)/src/data_exchange/ew2openapi/json-c-build

# Flags del compilador
CFLAGS = -Wall -g -D_LINUX -D_REENTRANT -I$(EW_HOME)/$(EW_VERSION)/include \
         -I$(JSONCDIR)/include -I$(KAFKADIR)/include

# Flags del linker
LDFLAGS = -lm -lpthread

# Librerias Estáticas
# Librería de Earthworm
LIB_EW = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
# Librerías externas
LIB_KAFKA = $(KAFKADIR)/lib/librdkafka.a
LIB_JSON = $(JSONCDIR)/lib/libjson-c.a

# Librerias adicionales que rdkafka requiere estáticamente en Linux modernos
EXT_LIBS = -lssl -lcrypto -lz -lzstd -ldl

all: ew2glass

ew2glass: ew2glass.o
	$(CC) $(CFLAGS) -o ew2glass ew2glass.o $(LIB_EW) $(LIB_KAFKA) $(LIB_JSON) $(LDFLAGS) $(EXT_LIBS)
	cp ew2glass $(EW_HOME)/$(EW_VERSION)/bin/

ew2glass.o: ew2glass.c
	$(CC) $(CFLAGS) -c ew2glass.c

clean:
	rm -f ew2glass *.o
