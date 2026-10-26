# Makefile para pickml
#
# Picker neuronal P/S sobre un grafo ONNX (EQTransformer).
# Enlaza contra libew_mt.a (kom/logit/anillos) como los demás csn*.
#
# El motor ONNX es OPCIONAL para COMPILAR (sin él queda el motor `stub` y los
# tests headless pasan), pero es imprescindible para OPERAR: el .d usa
# `Engine onnx` y sin el motor el arranque sale con exit 3 ("ONNX Runtime no
# compilado") y el módulo queda muerto bajo startstop. Por eso se AUTODETECTA
# el ORT vendorizado (el mismo que el .d carga por `OrtLib`); `make ORT_DIR=`
# fuerza el stub y `make ORT_DIR=<dir>` fija otro:
#
#   make ORT_DIR=/mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0
#
# (sólo hace falta el HEADER: la librería se carga con dlopen en runtime).
ORT_HEADER := $(firstword $(wildcard /mnt/d/resources/pickml/onnxruntime*/include/onnxruntime_c_api.h))
ifeq ($(origin ORT_DIR), undefined)
ORT_DIR := $(patsubst %/include/onnxruntime_c_api.h,%,$(ORT_HEADER))
endif
CFLAGS = -O2 -g -Wall -I. -I../common/include \
         -I$(EW_HOME)/$(EW_VERSION)/include -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread -ldl

# pickml_fetch es un binario APARTE a propósito: pickml no enlaza libpq (no
# habla con la BD). Desde que hay modo offline `--sds`, pickml SÍ enlaza
# `libmseed2.a` (estática, vendorizada) para leer miniSEED; el SDS comparte
# lector con el fetch.
FETCH        = pickml_fetch
LIBPQ_CFLAGS = $(shell pkg-config --cflags libpq)
LIBPQ_LIBS   = $(shell pkg-config --libs libpq)
MSEED_LIB    = $(EW_HOME)/$(EW_VERSION)/lib/libmseed2.a
FETCH_LIBS   = $(MSEED_LIB) $(LIBPQ_LIBS) $(EWLIBS) -lm

OBJS = pickml_main.o pickml_config.o pickml_sta.o pickml_buffer.o \
       pickml_window.o pickml_post.o pickml_report.o pickml_source.o \
       pickml_sds.o ewgui_sds.o pickml_ring.o pickml_engine.o pickml_publish.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

TARGET = pickml

# Ver AGENTS.md §5/§7: sin fijar el goal, `make` a secas compilaría objetos y
# NUNCA enlazaría ni instalaría el binario.
.DEFAULT_GOAL := all

$(OBJS): pickml.h
# ewgui_sds.c es puro (sin GTK) y se compila suelto, igual que en csnmags.
ewgui_sds.o: ../common/src/ewgui_sds.c
	$(CC) $(CFLAGS) -MMD -MP -MF ewgui_sds.o.dep -c -o $@ ../common/src/ewgui_sds.c
$(TESTS): pickml.h

ifdef ORT_DIR
# Sólo hace falta el HEADER: la librería se carga con dlopen en runtime desde
# la clave OrtLib del .d. Enlazarla aquí reintroduciría una dependencia de carga
# (y un rpath) que el diseño evita a propósito.
CFLAGS += -DPICKML_HAVE_ORT -I$(ORT_DIR)/include
OBJS += pickml_engine_onnx.o
endif

all: $(TARGET) $(FETCH) install
	@if [ -z "$(ORT_DIR)" ]; then \
	  echo "pickml: AVISO motor ONNX no compilado (stub): 'Engine onnx' fallara en runtime"; \
	fi

# Instalación atómica y SIEMPRE (aunque el binario esté corriendo o al día).
# Ver common/tools/install_bin.sh y AGENTS.md §7.
.PHONY: install
install: $(TARGET) $(FETCH)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(TARGET)
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin $(FETCH)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(EWLIBS) $(MSEED_LIB) $(LIBS)

$(FETCH): pickml_fetch.o pickml_sds.o ewgui_sds.o
	$(CC) $(CFLAGS) $(LIBPQ_CFLAGS) -o $@ pickml_fetch.o pickml_sds.o ewgui_sds.o $(FETCH_LIBS)

pickml_fetch.o: pickml_fetch.c pickml.h
	$(CC) $(CFLAGS) $(LIBPQ_CFLAGS) -c -o $@ pickml_fetch.c

# ---------------------------------------------------------------- tests ------
TESTS = test/test_buffer test/test_window test/test_post test/test_report \
        test/test_config test/test_sta test/test_publish test/gen_traces test/gen_sds

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_buffer ==="; ./test/test_buffer
	@echo "=== test_window ==="; ./test/test_window
	@echo "=== test_post ===";   ./test/test_post
	@echo "=== test_report ==="; ./test/test_report
	@echo "=== test_config ==="; ./test/test_config
	@echo "=== test_sta ===";    ./test/test_sta
	@echo "=== test_publish ==="; ./test/test_publish
	@echo "TODOS LOS TESTS OK"

# Modo offline determinista (sin anillos). Los tests usan el motor stub salvo
# test_eqt_d.sh, que valida el .d de EQT contra el modelo real si esta presente
# (y se SALTA si no: no rompe el build base).
check-offline: $(TARGET) test/gen_traces test/gen_sds
	sh ./test/test_offline.sh
	sh ./test/test_sds.sh
	sh ./test/test_publish_offline.sh
	sh ./test/test_eqt_d.sh
	sh ./test/test_eqt_d.sh pickml_iquique.d

# Paridad numérica contra el grafo real: exige ORT + modelo + golden fixtures.
# Se salta con un mensaje claro si no están (no falla el build base).
check-engine: $(TARGET)
	@if [ -z "$(ORT_DIR)" ]; then \
	  echo "check-engine: SKIP (construir con ORT_DIR=...)"; exit 0; \
	fi
	sh ./test/test_engine.sh

# Camino vivo de publicacion sobre el EarthWorm en marcha (no lo arranca: SKIP
# si no esta arriba). Valida fail-fast, attach y politica en --publish-dry-run.
test-live: $(TARGET)
	sh ./test/test_live.sh

test/test_buffer: test/test_buffer.c pickml_buffer.o
	$(CC) $(CFLAGS) -o $@ test/test_buffer.c pickml_buffer.o $(LIBS)

test/test_window: test/test_window.c pickml_window.o
	$(CC) $(CFLAGS) -o $@ test/test_window.c pickml_window.o $(LIBS)

test/test_post: test/test_post.c pickml_post.o
	$(CC) $(CFLAGS) -o $@ test/test_post.c pickml_post.o $(LIBS)

test/test_report: test/test_report.c pickml_report.o
	$(CC) $(CFLAGS) -o $@ test/test_report.c pickml_report.o $(LIBS)

test/test_config: test/test_config.c pickml_config.o pickml_publish.o
	$(CC) $(CFLAGS) -o $@ test/test_config.c pickml_config.o pickml_publish.o $(EWLIBS) $(LIBS)

test/test_sta: test/test_sta.c pickml_sta.o pickml_config.o pickml_publish.o
	$(CC) $(CFLAGS) -o $@ test/test_sta.c pickml_sta.o pickml_config.o pickml_publish.o $(EWLIBS) $(LIBS)

test/test_publish: test/test_publish.c pickml_publish.o
	$(CC) $(CFLAGS) -o $@ test/test_publish.c pickml_publish.o $(LIBS)

test/gen_traces: test/gen_traces.c
	$(CC) $(CFLAGS) -o $@ test/gen_traces.c $(LIBS)

test/gen_sds: test/gen_sds.c ewgui_sds.o
	$(CC) $(CFLAGS) -o $@ test/gen_sds.c ewgui_sds.o $(MSEED_LIB) $(LIBS)

clean:
	rm -f *.o $(TARGET) $(FETCH) $(TESTS) test/*.o
	rm -rf test/tmp test/tmp_eqt

.PHONY: all clean test check check-offline check-engine test-live

# --- Validacion de configuracion (ew8_compile.sh --validate) ----------------
# Una entrada por comando, formato "binario|argumentos", separadas por `\n`
# LITERAL: make parte la receta en cada newline real de una variable expandida y
# el printf queda con la comilla sin cerrar. El .d vive en EW_PARAMS
# (run_working_v8/params). --print-config NO abre el motor ONNX ni el anillo.
VALIDATE_FLAGS = pickml|pickml_phasenetwc.d --print-config
.PHONY: print-VALIDATE_FLAGS
print-VALIDATE_FLAGS:
	@printf '%b\n' '$(VALIDATE_FLAGS)'

# --- Dependencias de cabeceras (ver AGENTS.md §5) ---------------------------
CFLAGS += -MMD -MP -MF $(@).dep
-include $(wildcard *.dep */*.dep)
clean: clean-deps
clean-deps:
	rm -f *.dep */*.dep
