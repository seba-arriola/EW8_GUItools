#!/usr/bin/env python3
"""Pruebas de api_report_locs (solo biblioteca estandar).

    python3 -m unittest discover -s api_report_locs/tests -v
    python3 api_report_locs/tests/test_report.py
"""

import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from api_report_locs import parsers, report  # noqa: E402

# -----------------------------------------------------------------------------
#  Lineas REALES de log (copiadas de run_working_v8/log/) como fixtures
# -----------------------------------------------------------------------------
CSNLOC_EVENT_LINES = [
    "20260922_UTC_16:39:26 csnloc: evento 1 lat=-27.737 lon=-75.168 z=0.0 km nph=5 rms=1.86 gap=265",
    "20260922_UTC_17:32:18 csnloc: evento 4 lat=-20.395 lon=-69.900 z=0.0 km nph=7 rms=0.80 gap=193",
    "20260922_UTC_18:49:34 csnloc: evento 13 lat=-21.961 lon=-69.679 z=22.5 km nph=4 rms=1.85 gap=117",
]
CSNLOC_EXIT_LINE = "20260922_UTC_20:57:11 csnloc: terminando (insertados=235 reemplazados=3)"
CSNLOC_INIT_LINE = "20260101_UTC_00:00:01 csnloc: iniciando (in=HYPO_RING out=PICK_RING threads=2)"
CSNLOC_STAS_LINE = "20260101_UTC_00:00:01 csnloc: 137 estaciones cargadas de estaciones_107.txt"

CSNMAGS_RED_LINES = [
    "20260918_UTC_17:00:30 CSNmags_Red: [ID 2013972921] ML=3.72 (1 est) | MWp=0.00 (0 est) -> PREF: ML 3.72",
    "20260918_UTC_17:03:02 CSNmags_Red: [ID 0820651320] ML=0.00 (0 est) | MWp=6.24 (1 est) -> PREF: None 0.00",
    "20260918_UTC_17:04:11 CSNmags_Red: [ID 0197433562] ML=0.00 (0 est) | MWp=5.61 (3 est) -> PREF: Mwp 5.61",
]
CSNMAGS_READY_LINE = "20260101_UTC_00:00:05 csnmags_toy: Listo. Esperando sismos en el anillo..."

WS_DISCARD_LINES = [
    "20260922_UTC_23:31:32 WARNING: Tracebuf fails validity check, discarding: <PB14.LHZ.CX.-->",
    "20260922_UTC_23:31:32 WARNING: Tracebuf fails validity check, discarding: <EFI.LHZ.II.00>",
]

# ARC volcado con DumpHypo 1 (16 primeros chars = YYYYMMDDHHMMSScc)
ARC_HEAD_LINE = "20260101_UTC_00:01:00 csnloc: --- HYP2000ARC evento 7 ---"
ARC_FIRST_LINE = "2015091622543312-31.5700-71.67000000" + " " * 140
ARC_END_LINE = "20260101_UTC_00:01:00 csnloc: --- fin HYP2000ARC evento 7 ---"


class TestLineParsers(unittest.TestCase):
    def test_csnloc_event(self):
        ev = parsers.parse_csnloc_event(CSNLOC_EVENT_LINES[0])
        self.assertIsNotNone(ev)
        self.assertEqual(ev.event_id, 1)
        self.assertAlmostEqual(ev.lat, -27.737)
        self.assertAlmostEqual(ev.lon, -75.168)
        self.assertEqual(ev.nph, 5)
        self.assertAlmostEqual(ev.rms, 1.86)
        self.assertEqual(ev.gap, 265)
        self.assertEqual(ev.log_time_utc, "2026-09-22T16:39:26Z")

    def test_csnloc_event_con_z_y_rms_variables(self):
        ev = parsers.parse_csnloc_event(CSNLOC_EVENT_LINES[2])
        self.assertAlmostEqual(ev.depth_km, 22.5)
        self.assertEqual(ev.event_id, 13)

    def test_csnloc_event_no_matchea_otras_lineas(self):
        for line in (CSNLOC_EXIT_LINE, CSNLOC_INIT_LINE, CSNLOC_STAS_LINE, ""):
            self.assertIsNone(parsers.parse_csnloc_event(line))

    def test_csnloc_exit(self):
        self.assertEqual(parsers.parse_csnloc_exit(CSNLOC_EXIT_LINE), (235, 3))
        self.assertIsNone(parsers.parse_csnloc_exit(CSNLOC_EVENT_LINES[0]))

    def test_csnmags_red_pref_ml(self):
        m = parsers.parse_csnmags_red(CSNMAGS_RED_LINES[0])
        self.assertEqual(m.event_id, 2013972921)
        self.assertAlmostEqual(m.ml, 3.72)
        self.assertEqual(m.n_ml, 1)
        self.assertAlmostEqual(m.mwp, 0.0)
        self.assertEqual(m.n_mwp, 0)
        self.assertEqual(m.pref, "ML")
        self.assertAlmostEqual(m.pref_val, 3.72)

    def test_csnmags_red_pref_none(self):
        m = parsers.parse_csnmags_red(CSNMAGS_RED_LINES[1])
        self.assertEqual(m.event_id, 820651320)   # ceros a la izquierda ignorados
        self.assertEqual(m.pref, "None")
        self.assertEqual(m.n_ml, 0)
        self.assertEqual(m.n_mwp, 1)

    def test_csnmags_red_pref_mwp(self):
        m = parsers.parse_csnmags_red(CSNMAGS_RED_LINES[2])
        self.assertEqual(m.pref, "Mwp")
        self.assertAlmostEqual(m.mwp, 5.61)
        self.assertEqual(m.n_mwp, 3)

    def test_arc_stamp_to_iso(self):
        self.assertEqual(parsers.arc_stamp_to_iso("2015091622543312"), "2015-09-16T22:54:33.12Z")
        self.assertEqual(parsers.arc_stamp_to_iso("20150916225433"), "2015-09-16T22:54:33.00Z")
        self.assertIsNone(parsers.arc_stamp_to_iso("no-es-fecha"))


class TestFileParsers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="api_report_locs_")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _write(self, name, lines):
        p = os.path.join(self.tmp, name)
        with open(p, "w", encoding="utf-8") as fh:
            for line in lines:
                fh.write(line + "\n")
        return p

    def test_csnloc_file_con_arc(self):
        p = self._write("csnloc_20260101.log", [
            CSNLOC_INIT_LINE, CSNLOC_STAS_LINE,
            *CSNLOC_EVENT_LINES,
            ARC_HEAD_LINE, ARC_FIRST_LINE, ARC_END_LINE,
            CSNLOC_EXIT_LINE,
        ])
        info = parsers.parse_csnloc_file(p)
        self.assertTrue(info["started"])
        self.assertEqual(info["stations"], 137)
        self.assertEqual(len(info["events"]), 3)
        self.assertEqual(info["exits"], [(235, 3)])
        # el evento 7 del ARC no esta entre los eventos logueados -> no se inventa
        self.assertNotIn(7, [e.event_id for e in info["events"]])
        self.assertIn(7, info["arc_origins"])
        self.assertEqual(info["arc_origins"][7], "2015-09-16T22:54:33.12Z")

    def test_csnloc_file_arc_asocia_hora_origen(self):
        ev_line = "20260101_UTC_00:01:00 csnloc: evento 7 lat=-31.570 lon=-71.670 z=5.0 km nph=12 rms=0.50 gap=90"
        p = self._write("csnloc2.log", [
            CSNLOC_INIT_LINE, ev_line,
            ARC_HEAD_LINE, ARC_FIRST_LINE, ARC_END_LINE,
        ])
        info = parsers.parse_csnloc_file(p)
        self.assertEqual(len(info["events"]), 1)
        self.assertEqual(info["events"][0].origin_time_utc, "2015-09-16T22:54:33.12Z")

    def test_csnmags_file_ultima_por_id(self):
        p = self._write("csnmags.log", [
            CSNMAGS_READY_LINE,
            "20260101_UTC_00:02:00 CSNmags_Red: [ID 0000000007] ML=3.00 (1 est) | MWp=0.00 (0 est) -> PREF: ML 3.00",
            "20260101_UTC_00:03:00 CSNmags_Red: [ID 0000000007] ML=3.55 (4 est) | MWp=5.90 (2 est) -> PREF: Mwp 5.90",
        ])
        info = parsers.parse_csnmags_file(p)
        self.assertTrue(info["ready"])
        self.assertEqual(len(info["magnitudes"]), 1)      # la ultima gana
        self.assertAlmostEqual(info["magnitudes"][0].ml, 3.55)
        self.assertEqual(info["magnitudes"][0].n_ml, 4)

    def test_wave_server_discards(self):
        p = self._write("wsv.log", WS_DISCARD_LINES + ["otra linea"])
        info = parsers.parse_wave_server_file(p)
        self.assertEqual(info["discards"], 2)
        self.assertIn("PB14.LHZ.CX.--", info["scnls"])


class TestCorrelacionYDuplicados(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="api_report_locs_")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _mk_session(self, slug, idx, events, mags, discards=0, ready=True):
        sdir = os.path.join(self.tmp, slug, "%03d" % idx)
        ldir = os.path.join(sdir, "logs")
        os.makedirs(ldir, exist_ok=True)
        csl = os.path.join(ldir, "csnloc_20260101.log")
        cml = os.path.join(ldir, "csnmags_toy_20260101.log")
        wsv = os.path.join(ldir, "wave_serverV_20260101.log")
        with open(csl, "w") as fh:
            fh.write(CSNLOC_INIT_LINE + "\n")
            for e in events:
                fh.write(e + "\n")
        with open(cml, "w") as fh:
            if ready:
                fh.write(CSNMAGS_READY_LINE + "\n")
            for m in mags:
                fh.write(m + "\n")
        with open(wsv, "w") as fh:
            for _ in range(discards):
                fh.write(WS_DISCARD_LINES[0] + "\n")
        meta = {
            "session_id": "%s/%03d" % (slug, idx), "slug": slug, "source_file": "mseed/%s.mseed" % slug,
            "chunk_index": idx, "chunk_start_utc": "2026-01-01T00:00:00Z", "mode": "fast",
            "wall_s": 80, "exit_code": 0, "timeout": False,
            "counts": {"events": len(events), "magnitudes": len(mags), "ws_discards": discards},
            "logs": {"csnloc": csl, "csnmags_toy": cml, "wave_serverV": wsv},
        }
        with open(os.path.join(sdir, "session.json"), "w") as fh:
            json.dump(meta, fh)
        return os.path.join(sdir, "session.json")

    def test_cruce_por_id_intra_sesion(self):
        ev = "20260101_UTC_00:01:00 csnloc: evento 7 lat=-31.570 lon=-71.670 z=5.0 km nph=12 rms=0.50 gap=90"
        mg = "20260101_UTC_00:02:00 CSNmags_Red: [ID 0000000007] ML=6.10 (5 est) | MWp=7.80 (3 est) -> PREF: Mwp 7.80"
        self._mk_session("ev_a", 0, [ev], [mg])
        sessions = [report.load_session(p) for p in report.find_sessions(self.tmp)]
        rows = report.build_rows(sessions)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].event_id, 7)
        self.assertAlmostEqual(rows[0].ml, 6.10)
        self.assertAlmostEqual(rows[0].mwp, 7.80)
        self.assertEqual(rows[0].pref, "Mwp")
        self.assertEqual(rows[0].source_file, "mseed/ev_a.mseed")

    def test_evento_sin_magnitud_se_marca(self):
        ev = "20260101_UTC_00:01:00 csnloc: evento 3 lat=-20.0 lon=-70.0 z=10.0 km nph=6 rms=1.00 gap=120"
        self._mk_session("ev_b", 0, [ev], [])
        sessions = [report.load_session(p) for p in report.find_sessions(self.tmp)]
        rows = report.build_rows(sessions)
        self.assertEqual(len(rows), 1)
        self.assertIn("sin magnitud", rows[0].notes)

    def test_magnitud_sin_localizacion_se_marca(self):
        mg = "20260101_UTC_00:02:00 CSNmags_Red: [ID 0000000009] ML=4.00 (2 est) | MWp=0.00 (0 est) -> PREF: ML 4.00"
        self._mk_session("ev_c", 0, [], [mg])
        sessions = [report.load_session(p) for p in report.find_sessions(self.tmp)]
        rows = report.build_rows(sessions)
        self.assertEqual(len(rows), 1)
        self.assertIn("sin localizacion", rows[0].notes)

    def test_duplicados_por_solape(self):
        ev0 = "20260101_UTC_00:01:00 csnloc: evento 1 lat=-31.570 lon=-71.670 z=5.0 km nph=12 rms=0.50 gap=90"
        ev1 = "20260101_UTC_00:01:30 csnloc: evento 1 lat=-31.575 lon=-71.668 z=5.0 km nph=11 rms=0.60 gap=95"
        self._mk_session("ev_d", 0, [ev0], [])
        self._mk_session("ev_d", 1, [ev1], [])
        sessions = [report.load_session(p) for p in report.find_sessions(self.tmp)]
        rows = report.build_rows(sessions)
        n = report.flag_duplicates(rows)
        self.assertEqual(n, 1)
        self.assertTrue(all(r.dup for r in rows))

    def test_sesion_avisa_si_csnmags_no_arranco(self):
        ev = "20260101_UTC_00:01:00 csnloc: evento 1 lat=-31.0 lon=-71.0 z=1.0 km nph=5 rms=1.0 gap=100"
        self._mk_session("ev_e", 0, [ev], [], ready=False)
        sessions = [report.load_session(p) for p in report.find_sessions(self.tmp)]
        self.assertFalse(sessions[0].csnmags_ready)
        self.assertTrue(any("csnmags_toy no llego" in w for w in sessions[0].warnings))


class TestCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="api_report_locs_")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _run(self, argv):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            rc = report.main(argv)
        return rc, buf.getvalue()

    def test_sin_sesiones(self):
        rc, out = self._run(["--runs", os.path.join(self.tmp, "vacio")])
        self.assertEqual(rc, report.EXIT_NO_SESSIONS)
        self.assertIn("no hay sesiones", out)

    def test_con_evento_y_salidas(self):
        slug = "ev_x"
        sdir = os.path.join(self.tmp, slug, "000")
        ldir = os.path.join(sdir, "logs")
        os.makedirs(ldir)
        csl = os.path.join(ldir, "csnloc_20260101.log")
        cml = os.path.join(ldir, "csnmags_toy_20260101.log")
        with open(csl, "w") as fh:
            fh.write(CSNLOC_INIT_LINE + "\n")
            fh.write("20260101_UTC_00:01:00 csnloc: evento 7 lat=-31.570 lon=-71.670 z=5.0 km nph=12 rms=0.50 gap=90\n")
        with open(cml, "w") as fh:
            fh.write(CSNMAGS_READY_LINE + "\n")
            fh.write("20260101_UTC_00:02:00 CSNmags_Red: [ID 0000000007] ML=6.10 (5 est) | MWp=7.80 (3 est) -> PREF: Mwp 7.80\n")
        with open(os.path.join(sdir, "session.json"), "w") as fh:
            json.dump({"session_id": "ev_x/000", "slug": slug, "source_file": "mseed/ev_x.mseed",
                       "chunk_index": 0, "wall_s": 80, "exit_code": 0, "timeout": False,
                       "counts": {}, "logs": {"csnloc": csl, "csnmags_toy": cml}}, fh)

        out_prefix = os.path.join(self.tmp, "rep")
        rc, out = self._run(["--runs", self.tmp, "--format", "all", "--out", out_prefix])
        self.assertEqual(rc, report.EXIT_OK)
        self.assertIn("EVENTOS LOCALIZADOS", out)
        self.assertIn("ev_x", out)
        self.assertIn("Mwp", out)
        self.assertTrue(os.path.isfile(out_prefix + ".csv"))
        self.assertTrue(os.path.isfile(out_prefix + ".json"))
        with open(out_prefix + ".json") as fh:
            doc = json.load(fh)
        self.assertEqual(doc["summary"]["events"], 1)
        self.assertEqual(doc["summary"]["with_magnitude"], 1)
        self.assertEqual(doc["events"][0]["event_id"], 7)

    def test_sin_eventos_sale_2(self):
        sdir = os.path.join(self.tmp, "ev_vacio", "000")
        ldir = os.path.join(sdir, "logs")
        os.makedirs(ldir)
        csl = os.path.join(ldir, "csnloc_20260101.log")
        with open(csl, "w") as fh:
            fh.write(CSNLOC_INIT_LINE + "\n")
        with open(os.path.join(sdir, "session.json"), "w") as fh:
            json.dump({"session_id": "ev_vacio/000", "slug": "ev_vacio", "logs": {"csnloc": csl}}, fh)
        rc, _ = self._run(["--runs", self.tmp, "--quiet"])
        self.assertEqual(rc, report.EXIT_NO_EVENTS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
