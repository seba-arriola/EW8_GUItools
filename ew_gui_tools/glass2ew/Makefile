CC = gcc

# Directorios de librerias externas (Ajusta si es necesario)
KAFKADIR = $(EW_HOME)/$(EW_VERSION)/src/data_exchange/ew2openapi/librdkafka/build
JSONCDIR = $(EW_HOME)/$(EW_VERSION)/src/data_exchange/ew2openapi/json-c-build

# Directorio donde se encuentra pipe.c
LIBSRC_UNIX = $(EW_HOME)/$(EW_VERSION)/src/libsrc/unix

# Flags del compilador
CFLAGS = -Wall -g -D_LINUX -D_REENTRANT -I$(EW_HOME)/$(EW_VERSION)/include \
         -I$(JSONCDIR)/include -I$(KAFKADIR)/include

# Flags del linker
LDFLAGS = -lm -lpthread

# Librerias Estaticas
LIB_EW = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a
LIB_KAFKA = $(KAFKADIR)/lib/librdkafka.a
LIB_JSON = $(JSONCDIR)/lib/libjson-c.a

# Librerias adicionales que rdkafka requiere estaticamente
EXT_LIBS = -lssl -lcrypto -lz -lzstd -ldl

all: glass2ew

# Ahora dependemos de glass2ew.o y pipe.o
glass2ew: glass2ew.o pipe.o
	$(CC) $(CFLAGS) -o glass2ew glass2ew.o pipe.o $(LIB_EW) $(LIB_KAFKA) $(LIB_JSON) $(LDFLAGS) $(EXT_LIBS)
	cp glass2ew $(EW_HOME)/$(EW_VERSION)/bin/

glass2ew.o: glass2ew.c
	$(CC) $(CFLAGS) -c glass2ew.c

# Compilar pipe.c directamente desde la carpeta libsrc/unix
pipe.o: $(LIBSRC_UNIX)/pipe.c
	$(CC) $(CFLAGS) -c $(LIBSRC_UNIX)/pipe.c

clean:
	rm -f glass2ew *.o
