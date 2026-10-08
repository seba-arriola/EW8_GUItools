# Makefile para pickS
#
# Picker automático de onda S (Earthworm nativo).
# Enlaza contra libew_mt.a como el resto de los csn*.

CFLAGS = -O2 -g -Wall -I. -I$(EW_HOME)/$(EW_VERSION)/include \
         -D_LINUX -D_REENTRANT

LIBS = -lm -lpthread

OBJS = pickS.o config.o sta_list.o filters.o ring3c.o stalta.o aic.o \
       polarization.o quality.o report.o picks_ref.o

EWLIBS = $(EW_HOME)/$(EW_VERSION)/lib/libew_mt.a

TARGET = pickS

# Todos los objetos dependen del header (evita layouts de struct inconsistentes).
$(OBJS): pickS.h
$(TESTS): pickS.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(EWLIBS) $(LIBS)
	cp $(TARGET) $(EW_HOME)/$(EW_VERSION)/bin/

TESTS = test/test_filter test/test_stalta test/test_aic \
        test/test_polarization test/test_quality test/test_ring3c \
        test/test_report test/test_sta_list test/gen_tank

test: $(TESTS)
	@echo "Tests compilados. Ejecutar con make check"

check: $(TESTS) $(TARGET)
	@echo "=== test_filter ===";       ./test/test_filter
	@echo "=== test_stalta ===";       ./test/test_stalta
	@echo "=== test_aic ===";          ./test/test_aic
	@echo "=== test_polarization ==="; ./test/test_polarization
	@echo "=== test_quality ===";      ./test/test_quality
	@echo "=== test_ring3c ===";       ./test/test_ring3c
	@echo "=== test_report ===";       ./test/test_report
	@echo "=== test_sta_list ===";     ./test/test_sta_list
	@echo "TODOS LOS TESTS OK"

test/test_filter: test/test_filter.c filters.o
	$(CC) $(CFLAGS) -o $@ test/test_filter.c filters.o $(LIBS)

test/test_stalta: test/test_stalta.c stalta.o
	$(CC) $(CFLAGS) -o $@ test/test_stalta.c stalta.o $(LIBS)

test/test_aic: test/test_aic.c aic.o
	$(CC) $(CFLAGS) -o $@ test/test_aic.c aic.o $(LIBS)

test/test_polarization: test/test_polarization.c polarization.o
	$(CC) $(CFLAGS) -o $@ test/test_polarization.c polarization.o $(LIBS)

test/test_quality: test/test_quality.c quality.o config.o
	$(CC) $(CFLAGS) -o $@ test/test_quality.c quality.o config.o $(EWLIBS) $(LIBS)

test/test_ring3c: test/test_ring3c.c ring3c.o filters.o stalta.o
	$(CC) $(CFLAGS) -o $@ test/test_ring3c.c ring3c.o filters.o stalta.o $(LIBS)

test/test_report: test/test_report.c report.o picks_ref.o
	$(CC) $(CFLAGS) -o $@ test/test_report.c report.o picks_ref.o $(LIBS)

test/test_sta_list: test/test_sta_list.c sta_list.o
	$(CC) $(CFLAGS) -o $@ test/test_sta_list.c sta_list.o $(LIBS)

test/gen_tank: test/gen_tank.c
	$(CC) $(CFLAGS) -o $@ test/gen_tank.c $(LIBS)

# Test integrado del modo offline (determinista, sin anillos ni Earthworm).
check-offline: $(TARGET) test/gen_tank
	sh ./test/test_offline.sh

clean:
	rm -f *.o $(TARGET) $(TESTS) test/*.o

.PHONY: all clean test check check-offline
