# =============================================================================
#  tank_tools/Makefile - pruebas de las herramientas de replay y runner de eventos
#
#     make test        pruebas OFFLINE: no arrancan ningun servicio
#     make test-slow   OFFLINE + las lentas (mseed real: ~45 s, ~1,3 GB)
#     make test-live   OFFLINE + LIVE: arrancan y DETIENEN EarthWorm
#     make clean       borra tests/tmp
#
#     make tank-repo   construye el repo de tanks desde mseed/  (~2 min por fichero)
#     make events-plan planifica las sesiones de TARGET   (TARGET=tank_repo por def)
#     make events      ejecuta las sesiones de TARGET
#     make report      reporte de resultados (api_report_locs)
#     make py-test     pruebas del paquete api_report_locs
#
#     make capture     captura picks de TANKS -> PICKS/<marca de tiempo>/
#     make validate    csnloc OFFLINE sobre CAPTURE=... + reporte
#     make offline     capture + validate (usa la captura recien creada)
#     make report-offline  reporte de VALIDATION=... (o comparacion con CONTRA=...)
# =============================================================================

SHELL     := /bin/bash
TESTS_DIR := tests
TARGET    ?= tank_repo
TANKS     ?= tank_repo
PICKS     ?= picks
CAPTURE    ?=
VALIDATION ?=
CONTRA     ?=

.PHONY: all help test test-slow test-live clean \
        tank-repo events-plan events report py-test \
        capture validate offline report-offline

all: help

help:
	@echo "Targets de tank_tools:"
	@echo "  make test        - pruebas OFFLINE (no arrancan nada)"
	@echo "  make test-slow   - OFFLINE + las lentas (mseed real)"
	@echo "  make test-live   - LIVE (arranca y detiene EarthWorm; ~15 min)"
	@echo "  make tank-repo   - construye tank_repo/ desde mseed/"
	@echo "  make events-plan - planifica las sesiones (TARGET=$(TARGET))"
	@echo "  make events      - ejecuta las sesiones (TARGET=$(TARGET))"
	@echo "  make report      - reporte de resultados en runs/"
	@echo "  make py-test     - pruebas de api_report_locs"
	@echo "  make capture     - captura picks de $(TANKS) -> $(PICKS)/<ts>/"
	@echo "  make validate    - csnloc OFFLINE sobre CAPTURE=<captura> + reporte"
	@echo "  make offline     - capture + validate de la captura nueva"
	@echo "  make report-offline - reporte de VALIDATION=... (CONTRA=... para comparar)"
	@echo "  make clean       - borra $(TESTS_DIR)/tmp"

test:
	TANK_TESTS_SLOW=0 $(SHELL) $(TESTS_DIR)/run_all.sh

test-slow:
	TANK_TESTS_SLOW=1 $(SHELL) $(TESTS_DIR)/run_all.sh

test-live:
	TANK_TESTS_SLOW=0 $(SHELL) $(TESTS_DIR)/run_all.sh --live

tank-repo:
	cd .. && ./tank_tools/build_tank_repo.sh

events-plan:
	cd .. && ./tank_tools/run_events.sh plan $(TARGET)

events:
	cd .. && ./tank_tools/run_events.sh run $(TARGET)

report:
	cd .. && python3 -m api_report_locs.report --runs runs --format all

py-test:
	cd .. && python3 -m unittest discover -s api_report_locs/tests -t . -v

capture:
	cd .. && ./tank_tools/capture_picks.sh $(TANKS) $(PICKS)

validate:
	@test -n "$(CAPTURE)" || { echo "uso: make validate CAPTURE=$(PICKS)/<captura>"; exit 1; }
	cd .. && ./tank_tools/validate_csnloc.sh $(CAPTURE)

offline: capture
	@cd .. && cap="$$(ls -1d $(PICKS)/*/ 2>/dev/null | sort | tail -1)"; \
	 if [ -z "$$cap" ]; then echo "no hay capturas en $(PICKS)/"; exit 1; fi; \
	 ./tank_tools/validate_csnloc.sh "$$cap"

report-offline:
	@test -n "$(VALIDATION)" || { echo "uso: make report-offline VALIDATION=$(PICKS)/<captura>/csnlocvalidate_<ts> [CONTRA=...]"; exit 1; }
	cd .. && python3 tank_tools/offline_report.py $(VALIDATION) $(CONTRA)

clean:
	rm -rf $(TESTS_DIR)/tmp
