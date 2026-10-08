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
PICKS_S   ?= picks_s
PICKS_PS  ?= picks_ps
PPICKS    ?= picks
CAPTURE    ?=
CAPTURE_S  ?=
CAPTURE_PS ?=
VALIDATION ?=
VALIDATION_S ?=
VALIDATION_PS ?=
CONTRA     ?=
MANUAL_DAT  ?= picks_por_tests410.dat
MANUAL_WORK ?= /tmp/opencode/picks_manual
MANUAL_ONLY ?= *
MANUAL_RANGE ?=
MANUAL_MODE ?= both
MANUAL_MATCH_TOL ?= 2.0
MANUAL_RESID_TOL ?= 2.0
MANUAL_MAX_WEIGHT ?= 4

.PHONY: all help test test-slow test-live clean \
        tank-repo events-plan events report py-test \
        capture validate offline report-offline \
        capture-s validate-s offline-s report-offline-s \
        capture-ps validate-ps offline-ps report-offline-ps \
        validate-manual selftest-manual \
        report-catalog calibrate \
        refine-baseline calibrate-refiners report-refine

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
	@echo "  make capture-s   - captura picks S de $(TANKS) -> $(PICKS_S)/<ts>/"
	@echo "  make validate-s  - csnloc OFFLINE sobre CAPTURE_S=<captura S> + reporte"
	@echo "  make offline-s   - capture-s + validate-s de la captura nueva"
	@echo "  make capture-ps  - barrido P+S de $(TANKS) -> $(PICKS_PS)/<ts>/ (combinado)"
	@echo "  make validate-ps - csnloc OFFLINE sobre CAPTURE_PS=<captura combinada> + reporte"
	@echo "  make offline-ps  - capture-ps + validate-ps de la captura nueva"
	@echo "  make report-offline-ps - reporte de VALIDATION_PS=... (CONTRA=... para comparar)"
	@echo "  make report-catalog - calidad vs catalogo (VALIDATION=..., CATALOG=$(CATALOG))"
	@echo "  make calibrate   - barrido de parametros de csnloc (CAPTURE_PS=..., CALIB_ARGS=...)"
	@echo "  make refine-baseline - suelo de ARC desde una captura (PICKS_PS=..., REFINE_ARGS=...)"
	@echo "  make calibrate-refiners - barrido de refinadores (REFINER=hyp2000|nlloc, REFINE_ARGS=...)"
	@echo "  make report-refine - reporte A de una corrida (VALIDATION=<work>/val/<tag>)"
	@echo "  make validate-manual - ensayo pickS vs picadas manuales (MANUAL_ONLY=*, MANUAL_MODE=both)"
	@echo "  make selftest-manual - selftests de picks_s_manual.py y gen_guide_p.py"
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

capture-s:
	cd .. && ./tank_tools/capture_picks_s.sh --ppicks $(PPICKS) $(TANKS) $(PICKS_S)

validate-s:
	@test -n "$(CAPTURE_S)" || { echo "uso: make validate-s CAPTURE_S=$(PICKS_S)/<captura>"; exit 1; }
	cd .. && ./tank_tools/validate_csnloc.sh $(CAPTURE_S)

offline-s: capture-s
	@cd .. && cap="$$(ls -1d $(PICKS_S)/*/ 2>/dev/null | sort | tail -1)"; \
	 if [ -z "$$cap" ]; then echo "no hay capturas en $(PICKS_S)/"; exit 1; fi; \
	 ./tank_tools/validate_csnloc.sh "$$cap"

report-offline-s:
	@test -n "$(VALIDATION_S)" || { echo "uso: make report-offline-s VALIDATION_S=$(PICKS_S)/<captura>/csnlocvalidate_<ts> [CONTRA=...]"; exit 1; }
	cd .. && python3 tank_tools/offline_report.py $(VALIDATION_S) $(CONTRA)

capture-ps:
	cd .. && ./tank_tools/capture_picks_ps.sh $(TANKS) $(PICKS_PS)

validate-ps:
	@test -n "$(CAPTURE_PS)" || { echo "uso: make validate-ps CAPTURE_PS=$(PICKS_PS)/<captura>"; exit 1; }
	cd .. && ./tank_tools/validate_csnloc.sh $(CAPTURE_PS)

offline-ps: capture-ps
	@cd .. && cap="$$(ls -1d $(PICKS_PS)/*/ 2>/dev/null | sort | tail -1)"; \
	 if [ -z "$$cap" ]; then echo "no hay capturas en $(PICKS_PS)/"; exit 1; fi; \
	 ./tank_tools/validate_csnloc.sh "$$cap"

report-offline-ps:
	@test -n "$(VALIDATION_PS)" || { echo "uso: make report-offline-ps VALIDATION_PS=$(PICKS_PS)/<captura>/csnlocvalidate_<ts> [CONTRA=...]"; exit 1; }
	cd .. && python3 tank_tools/offline_report.py $(VALIDATION_PS) $(CONTRA)

# Calidad de las soluciones offline contra el catalogo de referencia.
#   make report-catalog VALIDATION=... [CATALOG=...] [REPORT_CAT_ARGS="..."]
CATALOG ?= tests_soluciones_publicadas.dat
REPORT_CAT_ARGS ?=
report-catalog:
	@test -n "$(VALIDATION)" || { echo "uso: make report-catalog VALIDATION=$(PICKS_PS)/<captura>/csnlocvalidate_<ts>"; exit 1; }
	cd .. && python3 tank_tools/catalog_report.py $(VALIDATION) --catalog $(CATALOG) $(REPORT_CAT_ARGS)

# Barrido de parametros de csnloc contra el catalogo (aislado; no toca csnloc.d).
#   make calibrate CAPTURE_PS=... [CALIB_ARGS="--only 'test[1-100]' --jobs 4"]
CALIB_WORK ?= /tmp/opencode/calib
CALIB_ARGS ?=
calibrate:
	@test -n "$(CAPTURE_PS)" || { echo "uso: make calibrate CAPTURE_PS=$(PICKS_PS)/<captura>"; exit 1; }
	cd .. && python3 tank_tools/calibrate_csnloc.py --capture $(CAPTURE_PS) \
	    --catalog $(CATALOG) --work $(CALIB_WORK) $(CALIB_ARGS)

# Suelo de ARC (el crudo de csnloc) y barrido de los refinadores. Todo OFFLINE.
#   make refine-baseline [PICKS_PS=...] [REFINE_ARGS="--only 'test[1-40]'"]
#   make calibrate-refiners REFINER=hyp2000|nlloc [REFINE_ARGS="--jobs 8"]
REFINE_BASE ?= tmp/refine
REFINE_WORK ?=
REFINE_ARGS ?=
refine-baseline:
	@cd .. && cap="$(PICKS_PS)"; \
	 if [ -z "$$cap" ]; then cap="$$(ls -1d picks_ps/*/ 2>/dev/null | sort | tail -1)"; fi; \
	 if [ -z "$$cap" ]; then echo "no hay capturas en picks_ps/"; exit 1; fi; \
	 echo "captura: $$cap"; \
	 python3 tank_tools/refine_arcs.py --baseline \
	    --csnloc ew_gui_tools/csnloc/csnloc \
	    --csnloc-d run_working_v8/params/csnloc.d \
	    --picks-dir "$$cap" --out $(REFINE_BASE) $(REFINE_ARGS)

calibrate-refiners:
	@test -n "$(REFINER)" || { echo "uso: make calibrate-refiners REFINER=hyp2000|nlloc [REFINE_ARGS=\"--jobs 8\"]"; exit 1; }
	cd .. && python3 tank_tools/calibrate_refiners.py --refiner $(REFINER) \
	    --baseline $(REFINE_BASE) --catalog $(CATALOG) \
	    $(if $(REFINE_WORK),--work $(REFINE_WORK),) $(REFINE_ARGS)

report-refine:
	@test -n "$(VALIDATION)" || { echo "uso: make report-refine VALIDATION=<work>/val/<tag>"; exit 1; }
	cd .. && python3 tank_tools/refine_report.py $(VALIDATION) $(REFINE_ARGS)

validate-manual:
	cd .. && ./tank_tools/validate_manual.sh --dat $(MANUAL_DAT) --work $(MANUAL_WORK) \
	    --only '$(MANUAL_ONLY)' --range '$(MANUAL_RANGE)' --mode $(MANUAL_MODE) \
	    --match-tol $(MANUAL_MATCH_TOL) --resid-tol $(MANUAL_RESID_TOL) \
	    --max-weight $(MANUAL_MAX_WEIGHT)

selftest-manual:
	cd .. && python3 tank_tools/picks_s_manual.py selftest
	cd .. && python3 tank_tools/gen_guide_p.py --selftest

clean:
	rm -rf $(TESTS_DIR)/tmp
