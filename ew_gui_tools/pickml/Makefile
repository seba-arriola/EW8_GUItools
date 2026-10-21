# Makefile para pickml
#
# Picker neuronal P/S sobre un grafo ONNX (EQTransformer).
# Enlaza contra libew_mt.a (kom/logit/anillos) como los demás csn*.
#
# El motor ONNX es OPCIONAL: sin ORT_DIR el módulo compila con el motor `stub`
# y TODOS los tests headless pasan. Con ORT_DIR=<dir con include/ y lib/> se
# añade el motor real:
#
#   make ORT_DIR=/mnt/d/resources/pickml/onnxruntime
#
CFLAGS = -O2 -g -Wall -I. -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread -ldl

# pickml_fetch es un binario APARTE a propósito: pickml no enlaza libpq ni
# libmseed. Aquí sí, porque su trabajo es hablar con la BD y leer el SDS.
FETCH        = pickml_fetch
LIBPQ_CFLAGS = $(shell pkg-config --cflags libpq)
LIBPQ_LIBS   = $(shell pkg-config --libs libpq)
MSEED_LIB    = $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a
FETCH_LIBS   = $(MSEED_LIB) $(LIBPQ_LIBS) $(EWLIBS) -lm

OBJS = pickml_main.o pickml_config.o pickml_sta.o pickml_buffer.o \
       pickml_window.o pickml_post.o pickml_report.o pickml_source.o \
       pickml_ring.o pickml_engine.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

TARGET = pickml

# Ver AGENTS.md §5/§7: sin fijar el goal, `make` a secas compilaría objetos y
# NUNCA enlazaría ni instalaría el binario.
.DEFAULT_GOAL := all

$(OBJS): pickml.h
$(TESTS): pickml.h

ifdef ORT_DIR
# Sólo hace falta el HEADER: la librería se carga con dlopen en runtime desde
# la clave OrtLib del .d. Enlazarla aquí reintroduciría una dependencia de carga
# (y un rpath) que el diseño evita a propósito.
CFLAGS += -DPICKML_HAVE_ORT -I$(ORT_DIR)/include
OBJS += pickml_engine_onnx.o
endif

all: $(TARGET) $(FETCH) install

# Instalación atómica y SIEMPRE (aunque el binario esté corriendo o al día).
# Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET) $(FETCH)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(FETCH)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(EWLIBS) $(LIBS)

$(FETCH): pickml_fetch.o
	$(CC) $(CFLAGS) $(LIBPQ_CFLAGS) -o $@ pickml_fetch.o $(FETCH_LIBS)

pickml_fetch.o: pickml_fetch.c pickml.h
	$(CC) $(CFLAGS) $(LIBPQ_CFLAGS) -c -o $@ pickml_fetch.c

# ---------------------------------------------------------------- tests ------
TESTS = test/test_buffer test/test_window test/test_post test/test_report \
        test/test_config test/test_sta test/gen_tank

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_buffer ==="; ./test/test_buffer
	@echo "=== test_window ==="; ./test/test_window
	@echo "=== test_post ===";   ./test/test_post
	@echo "=== test_report ==="; ./test/test_report
	@echo "=== test_config ==="; ./test/test_config
	@echo "=== test_sta ===";    ./test/test_sta
	@echo "TODOS LOS TESTS OK"

# Modo offline determinista (sin anillos, sin modelo): motor stub.
check-offline: $(TARGET) test/gen_tank
	sh ./test/test_offline.sh

# Paridad numérica contra el grafo real: exige ORT + modelo + golden fixtures.
# Se salta con un mensaje claro si no están (no falla el build base).
check-engine: $(TARGET)
	@if [ -z "$(ORT_DIR)" ]; then \
	  echo "check-engine: SKIP (construir con ORT_DIR=...)"; exit 0; \
	fi
	sh ./test/test_engine.sh

test/test_buffer: test/test_buffer.c pickml_buffer.o
	$(CC) $(CFLAGS) -o $@ test/test_buffer.c pickml_buffer.o $(LIBS)

test/test_window: test/test_window.c pickml_window.o
	$(CC) $(CFLAGS) -o $@ test/test_window.c pickml_window.o $(LIBS)

test/test_post: test/test_post.c pickml_post.o
	$(CC) $(CFLAGS) -o $@ test/test_post.c pickml_post.o $(LIBS)

test/test_report: test/test_report.c pickml_report.o
	$(CC) $(CFLAGS) -o $@ test/test_report.c pickml_report.o $(LIBS)

test/test_config: test/test_config.c pickml_config.o
	$(CC) $(CFLAGS) -o $@ test/test_config.c pickml_config.o $(EWLIBS) $(LIBS)

test/test_sta: test/test_sta.c pickml_sta.o pickml_config.o
	$(CC) $(CFLAGS) -o $@ test/test_sta.c pickml_sta.o pickml_config.o $(EWLIBS) $(LIBS)

test/gen_tank: test/gen_tank.c
	$(CC) $(CFLAGS) -o $@ test/gen_tank.c $(LIBS)

clean:
	rm -f *.o $(TARGET) $(FETCH) $(TESTS) test/*.o
	rm -rf test/tmp

.PHONY: all clean test check check-offline check-engine

# --- Dependencias de cabeceras (ver AGENTS.md §5) ---------------------------
CFLAGS += -MMD -MP -MF $(@).dep
-include $(wildcard *.dep */*.dep)
clean: clean-deps
clean-deps:
	rm -f *.dep */*.dep
