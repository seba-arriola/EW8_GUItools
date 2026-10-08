#!/usr/bin/env python3
"""
picks_s_manual.py - Validación de los picks S de pickS contra picadas manuales
y contra la solución hipocentral conocida de cada evento.

Entradas:
  --dat       picks_por_tests410.dat (cabecera: id<TAB>t0<TAB>lat<TAB>lon<TAB>prof<TAB>mag<TAB>tipo;
              picadas: NET.STA.LOC.CHAN<TAB>ISO8601<TAB>P|S<TAB>manual)
  --picks-s   directorio con <slug>.picks (salida de capture_picks_s.sh)
  --events-tt salida de ttp (id<TAB>sta<TAB>delta<TAB>ttP<TAB>ttS)
  --picksta   pickS.sta (para clasificar cobertura por estación/canal)

Reglas:
  - El emparejamiento con las S manuales es POR NOMBRE DE ESTACIÓN (ignora
    net/loc/chan): una S manual en MT14.BHN valida una S detectada en MT14.HHN.
  - resid_S = t_detectado - (t0_manual + ttS). Si |resid_S| <= resid_tol y
    weight <= max_weight, el pick es "BIEN" y se guarda en good_s/.

Subcomandos: events | report | calibrate | selftest
"""

import argparse
import calendar
import csv
import fnmatch
import glob
import json
import os
import statistics
import sys
from datetime import datetime

# --------------------------------------------------------------------------
# Parseo
# --------------------------------------------------------------------------

def parse_iso_utc(s):
    s = s.strip()
    if s.endswith('Z') or s.endswith('z'):
        s = s[:-1]
    dt = datetime.strptime(s, "%Y-%m-%dT%H:%M:%S")
    return float(calendar.timegm(dt.timetuple()))


def parse_pick_time(s):
    s = s.strip()
    dt = datetime.strptime(s[:14], "%Y%m%d%H%M%S")
    frac = 0.0
    if len(s) > 14 and s[14] == '.':
        frac = float("0" + s[14:])
    return float(calendar.timegm(dt.timetuple())) + frac


def norm_loc(loc):
    return loc if loc else "--"


def split_dat_scnl(scnl):
    """El .dat manual usa NET.STA.LOC.CHAN (p. ej. C1.BI04..HHE)."""
    p = scnl.strip().split('.')
    while len(p) < 4:
        p.append('')
    return p[0], p[1], norm_loc(p[2]), p[3]      # net, sta, loc, chan


def split_picks_scnl(scnl):
    """pickS/pick_FP emiten STA.CHAN.NET.LOC (p. ej. MT10.HHE.C1.--)."""
    p = scnl.strip().split('.')
    while len(p) < 4:
        p.append('')
    return p[0], p[1], norm_loc(p[2]), p[3]      # sta, chan, net, loc


def parse_dat_text(text):
    events, picks = {}, {}
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip('\n')
        if not line.strip():
            continue
        f = [x.strip() for x in line.split('\t')]
        if len(f) >= 7 and f[0].isdigit() and 'T' in f[1]:
            eid = int(f[0])
            events[eid] = {
                'id': eid, 't0': parse_iso_utc(f[1]), 't0_utc': f[1],
                'lat': float(f[2]), 'lon': float(f[3]), 'depth': float(f[4]),
                'mag': float(f[5]), 'magtype': f[6],
            }
            picks.setdefault(eid, [])
            cur = eid
            continue
        if len(f) < 4 or cur is None:
            continue
        net, sta, loc, chan = split_dat_scnl(f[0])
        picks[cur].append({'net': net, 'sta': sta, 'loc': loc, 'chan': chan,
                           't': parse_iso_utc(f[1]), 'phase': f[2].upper(),
                           'who': f[3]})
    return events, picks


def parse_dat(path):
    with open(path, 'r') as fh:
        return parse_dat_text(fh.read())


def parse_picks_text(text):
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        f = line.split()
        if len(f) < 8:
            continue
        try:
            int(f[3])
        except ValueError:
            continue
        sta, chan, net, loc = split_picks_scnl(f[4])
        fmwt = f[5]
        weight = int(fmwt[1]) if len(fmwt) > 1 and fmwt[1].isdigit() else 4
        phase = 'S' if (len(f) >= 11 and f[10][:1].upper() == 'S') else 'P'
        out.append({'sta': sta, 'chan': chan, 'net': net, 'loc': loc,
                    't': parse_pick_time(f[6]), 'weight': weight,
                    'phase': phase, 'raw': line})
    return out


def parse_tt_text(text):
    tt = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        f = line.split()
        if len(f) < 5:
            continue
        try:
            eid = int(f[0]); delta = float(f[2]); tp = float(f[3]); ts = float(f[4])
        except ValueError:
            continue
        tt[(eid, f[1])] = {'delta': delta, 'tp': tp, 'ts': ts}
    return tt


def parse_picksta(path):
    stations = {}
    if not os.path.isfile(path):
        return stations
    with open(path, 'r') as fh:
        for line in fh:
            p = line.strip()
            if not p or p.startswith('#'):
                continue
            f = p.split()
            if len(f) < 8:
                continue
            try:
                flag = int(f[0])
            except ValueError:
                continue
            if not flag:
                continue
            stations.setdefault(f[2], set()).update([f[5], f[6], f[7]])
    return stations


METRIC_FLOAT = ('t', 'snr', 'stalta', 'rect', 'plan', 'inc', 'azi', 'hv', 'dtsp')


def parse_metrics_text(text):
    """Lineas 'pickS: METRICS sta=... snr=... ... verdict=keep|drop' -> dicts."""
    out = []
    for raw in text.splitlines():
        line = raw.strip()
        if 'pickS: METRICS' not in line:
            continue
        kv = {}
        for tok in line.split()[2:]:
            if '=' in tok:
                k, v = tok.split('=', 1)
                kv[k] = v
        try:
            d = {'sta': kv['sta'], 'chan': kv.get('chan', ''), 'net': kv.get('net', ''),
                 'loc': norm_loc(kv.get('loc', '')), 'w': int(kv.get('w', 4)),
                 'verdict': kv.get('verdict', '')}
            for k in METRIC_FLOAT:
                d[k] = float(kv[k])
        except (KeyError, ValueError):
            continue
        out.append(d)
    return out


def parse_metrics(path):
    with open(path, 'r') as fh:
        return parse_metrics_text(fh.read())


def parse_config_q(path):
    """Pares clave/valor numéricos de un pickS.d (Q_* y Guide*)."""
    q = {}
    if not path or not os.path.isfile(path):
        return q
    with open(path, 'r') as fh:
        for line in fh:
            p = line.split('#', 1)[0].strip()
            if not p:
                continue
            f = p.split()
            if len(f) >= 2:
                try:
                    q[f[0]] = float(f[1])
                except ValueError:
                    pass
    return q


# --------------------------------------------------------------------------
# Análisis
# --------------------------------------------------------------------------

def match_manual(manual, t, tol, sta=None):
    """S manual más cercana en tiempo; si `sta` se da, exige misma estación
    (el emparejamiento es por NOMBRE DE ESTACIÓN, ignora canal)."""
    best, bd = None, None
    for m in manual:
        if sta is not None and m['sta'] != sta:
            continue
        dt = abs(m['t'] - t)
        if dt <= tol and (bd is None or dt < bd):
            bd, best = dt, m
    return best, bd


def match_metrics(metrics, sta, t, tol=0.05):
    """Métrica del log cuyo (sta,t) coincide con el pick emitido."""
    best, bd = None, None
    for m in metrics:
        if m['sta'] != sta:
            continue
        dt = abs(m['t'] - t)
        if dt <= tol and (bd is None or dt < bd):
            bd, best = dt, m
    return best


def certeza_of(m, q):
    """ALTA/BAJA por parámetros intrínsecos (no usa el archivo)."""
    if m is None:
        return 'SIN_METRICA'
    if q:
        for key, mk in (('snr', 'snr'), ('stalta', 'stalta'), ('rect', 'rect'),
                        ('plan', 'plan'), ('hv', 'hv')):
            thr = q.get(key)
            if thr is not None and m[mk] < thr:
                return 'BAJA'
        imin, imax = q.get('incid_min'), q.get('incid_max')
        if imin is not None and m['inc'] < imin:
            return 'BAJA'
        if imax is not None and m['inc'] > imax:
            return 'BAJA'
        dmin, dmax = q.get('dtsp_min'), q.get('dtsp_max')
        if dmin is not None and m.get('dtsp', -1.0) < dmin:
            return 'BAJA'
        if dmax is not None and m.get('dtsp', -1.0) > dmax:
            return 'BAJA'
    return 'ALTA'


def analyze_event(ev, manual, detected, tt, picker, opts, win=None, metrics=None, q=None):
    rows = []
    c = {'en_ref': 0, 'sin_ref': 0, 'nueva': 0, 'fn': 0, 'bien': 0,
         'alta': 0, 'baja': 0, 'alta_en_ref': 0, 'alta_sin_ref': 0,
         'out_of_coverage': 0, 'chan_diff': 0, 'hh_covered': 0,
         'n_manual_s': 0, 'out_of_window': 0}
    manual_s = [m for m in manual if m['phase'] == 'S']
    c['n_manual_s'] = len(manual_s)

    for m in manual_s:
        if m['sta'] not in picker:
            c['out_of_coverage'] += 1
        elif m['chan'] not in picker[m['sta']]:
            c['chan_diff'] += 1
        else:
            c['hh_covered'] += 1

    for d in detected:
        if d['phase'] != 'S':
            continue
        if win is not None and not (win[0] <= d['t'] <= win[1]):
            c['out_of_window'] += 1
            continue
        ttrow = tt.get((ev['id'], d['sta']))
        resid = None
        tpred = ''
        if ttrow and ttrow['ts'] > 0:
            tpred = ev['t0'] + ttrow['ts']
            resid = d['t'] - tpred
        m, dtm = match_manual(manual_s, d['t'], opts['match_tol'], d['sta'])
        mt = match_metrics(metrics, d['sta'], d['t']) if metrics else None
        cer = certeza_of(mt, q) if q is not None else ''
        if m is not None:
            verdict = 'EN_REF'; c['en_ref'] += 1
        else:
            verdict = 'SIN_REF'; c['sin_ref'] += 1
            if resid is not None and abs(resid) <= opts['resid_tol']:
                c['nueva'] += 1
        if cer == 'ALTA':
            c['alta'] += 1
            if m is not None:
                c['alta_en_ref'] += 1
            else:
                c['alta_sin_ref'] += 1
        elif cer == 'BAJA':
            c['baja'] += 1
        bien = (resid is not None and abs(resid) <= opts['resid_tol']
                and d['weight'] <= opts['max_weight'])
        if bien:
            c['bien'] += 1
        rows.append({
            'event': ev['id'], 'slug': 'test%d' % ev['id'],
            'sta': d['sta'], 'chan': d['chan'], 'net': d['net'], 'loc': d['loc'],
            't': round(d['t'], 3),
            'tpred_S': round(tpred, 3) if tpred != '' else '',
            'resid_S': round(resid, 3) if resid is not None else '',
            'matched': 'si' if m else 'no',
            'en_ref': 'si' if m else 'no',
            'chan_manual': m['chan'] if m else '',
            'dt_manual': round(dtm, 3) if dtm is not None else '',
            'verdict': verdict, 'weight': d['weight'],
            'certeza': cer,
            'snr': round(mt['snr'], 3) if mt else '',
            'stalta': round(mt['stalta'], 3) if mt else '',
            'rect': round(mt['rect'], 4) if mt else '',
            'plan': round(mt['plan'], 4) if mt else '',
            'inc': round(mt['inc'], 2) if mt else '',
            'hv': round(mt['hv'], 4) if mt else '',
            'dtsp': round(mt['dtsp'], 3) if mt else '',
            'bien': 1 if bien else 0, 'raw': d['raw'],
        })

    for m in manual_s:
        hit = False
        for d in detected:
            if d['phase'] != 'S' or d['sta'] != m['sta']:
                continue
            if abs(d['t'] - m['t']) <= opts['match_tol']:
                hit = True
                break
        if not hit:
            c['fn'] += 1
    return rows, c


def find_picks_file(picks_dir, slug):
    return find_file(picks_dir, slug, '.picks')


def find_file(d, slug, ext):
    if not d:
        return None
    cand = os.path.join(d, slug + ext)
    if os.path.isfile(cand):
        return cand
    subs = sorted(glob.glob(os.path.join(d, '*', slug + ext)))
    return subs[-1] if subs else None


def load_manifests(tanks_dir):
    """[(slug, start_epoch, end_epoch, span_s), ...] desde */manifest.json."""
    out = []
    for mp in glob.glob(os.path.join(tanks_dir, '*', 'manifest.json')):
        try:
            with open(mp, 'r') as fh:
                d = json.load(fh)
            a = parse_iso_utc(d['start_utc'])
            b = parse_iso_utc(d['end_utc'])
            out.append((d['slug'], a, b, b - a))
        except Exception:
            continue
    return out


def map_event(eid, t0, manifests):
    """Slug del tank que contiene t0. Prefiere test<id>; si no, el de menor span.
    Devuelve None si ningún tank contiene el origen."""
    if not manifests:
        return 'test%d' % eid
    pref = 'test%d' % eid
    cands = [m for m in manifests if m[1] <= t0 <= m[2]]
    for m in cands:
        if m[0] == pref:
            return pref
    if cands:
        return min(cands, key=lambda m: m[3])[0]
    return None


def _stats(xs):
    if not xs:
        return {'n': 0}
    xs2 = sorted(xs)
    med = statistics.median(xs2)
    mad = statistics.median([abs(x - med) for x in xs2])
    p90 = xs2[min(len(xs2) - 1, int(0.9 * len(xs2)))]
    return {'n': len(xs2), 'median': round(med, 3), 'mad': round(mad, 3),
            'p90': round(p90, 3), 'min': round(xs2[0], 3), 'max': round(xs2[-1], 3)}


def _sweep_weight(all_rows):
    out = []
    for mw in range(0, 5):
        acc = [r for r in all_rows if r['weight'] <= mw]
        en = sum(1 for r in acc if r['matched'] == 'si')
        out.append({'max_weight': mw, 'accepted': len(acc), 'EN_REF': en,
                    'SIN_REF': len(acc) - en})
    return out


def _sweep_resid(all_rows):
    out = []
    for rt in (1.0, 2.0, 3.0, 5.0):
        acc = [r for r in all_rows
               if r['resid_S'] != '' and abs(r['resid_S']) <= rt]
        en = sum(1 for r in acc if r['matched'] == 'si')
        out.append({'resid_tol': rt, 'accepted': len(acc), 'EN_REF': en,
                    'SIN_REF': len(acc) - en})
    return out


def _metric_dist(all_rows, keys):
    out = {}
    for k in keys:
        xs = [r[k] for r in all_rows if r.get(k) not in ('', None)]
        out[k] = _stats(xs)
    return out


def summarize(all_rows, all_counters, n_events, n_events_picks, n_no_tank, opts):
    agg = {k: 0 for k in ('en_ref', 'sin_ref', 'nueva', 'fn', 'bien', 'alta', 'baja',
                          'alta_en_ref', 'alta_sin_ref',
                          'out_of_coverage', 'chan_diff', 'hh_covered',
                          'n_manual_s', 'out_of_window')}
    for c in all_counters:
        for k in agg:
            agg[k] += c.get(k, 0)
    resid = [r['resid_S'] for r in all_rows if r['resid_S'] != '']
    wt = {}
    for r in all_rows:
        wt[r['weight']] = wt.get(r['weight'], 0) + 1
    dtm = [r['dt_manual'] for r in all_rows if r['dt_manual'] != '']
    alta = agg['alta']
    en_ref = agg['en_ref']
    return {
        'events_total': n_events,
        'events_with_picks': n_events_picks,
        'events_without_tank': n_no_tank,
        'detected_S': len(all_rows),
        'out_of_window': agg['out_of_window'],
        'EN_REF': agg['en_ref'], 'SIN_REF': agg['sin_ref'],
        'NUEVA': agg['nueva'], 'FN': agg['fn'], 'BIEN': agg['bien'],
        'manual_S_total': agg['n_manual_s'],
        'coverage': {
            'out_of_coverage': agg['out_of_coverage'],
            'name_covered_chan_diff': agg['chan_diff'],
            'hh_covered': agg['hh_covered'],
        },
        'resid_S': _stats(resid),
        'weight_hist': dict(sorted(wt.items())),
        'weight_sweep': _sweep_weight(all_rows),
        'resid_sweep': _sweep_resid(all_rows),
        'certeza': {
            'alta': alta, 'alta_en_ref': agg['alta_en_ref'],
            'alta_sin_ref': agg['alta_sin_ref'], 'baja': agg['baja'],
            'pureza_alta': (round(agg['alta_en_ref'] / alta, 4) if alta else None),
        },
        'metric_dist': _metric_dist(all_rows, METRIC_FLOAT),
        'file_check': {
            'en_ref': en_ref, 'sin_ref': agg['sin_ref'],
            'dt_manual': _stats(dtm),
            'pct_dt_le_2s': (round(100.0 * sum(1 for x in dtm if abs(x) <= 2.0) / len(dtm), 1)
                             if dtm else None),
        },
        'params': opts,
    }


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

CSV_COLS = ['event', 'slug', 'sta', 'chan', 'net', 'loc', 't', 'tpred_S',
            'resid_S', 'matched', 'en_ref', 'chan_manual', 'dt_manual', 'verdict',
            'certeza', 'snr', 'stalta', 'rect', 'plan', 'inc', 'hv', 'dtsp',
            'weight', 'bien']


def in_range(eid, rng):
    if not rng:
        return True
    lo, hi = rng.split('-')
    return int(lo) <= eid <= int(hi)


def cmd_events(args):
    events, _ = parse_dat(args.dat)
    with open(args.out, 'w') as fh:
        for eid in sorted(events):
            if not in_range(eid, args.range):
                continue
            if args.only != '*' and not fnmatch.fnmatch('test%d' % eid, args.only):
                continue
            ev = events[eid]
            fh.write("%d\t%.6f\t%.6f\t%.4f\n" % (eid, ev['lat'], ev['lon'], ev['depth']))
    print("[events] %s" % args.out)


def build_q(args, cfgq):
    """Umbrales de certeza: CLI > pickS.d (Q_*_W4) > deshabilitado."""
    def sel(attr, key):
        v = getattr(args, attr, None)
        return v if v is not None else cfgq.get(key)

    q = {'snr': sel('q_snr', 'Q_Snr_W4'),
         'stalta': sel('q_stalta', 'Q_StaLta_W4'),
         'rect': sel('q_rect', 'Q_Rect_W4'),
         'incid_min': sel('q_incid_min', 'Q_IncidMinDeg'),
         'incid_max': sel('q_incid_max', 'Q_IncidMaxDeg'),
         'dtsp_min': sel('q_dtsp_min', 'GuideMinDtSec'),
         'dtsp_max': sel('q_dtsp_max', 'GuideMaxDtSec'),
         'plan': sel('q_plan', 'Q_Plan_W4'),
         'hv': sel('q_hv', 'Q_Hv_W4')}
    for k in ('plan', 'hv'):
        if not q[k]:
            q[k] = None
    return q


def cmd_report(args):
    events, picks = parse_dat(args.dat)
    tt = parse_tt_text(open(args.events_tt).read()) if args.events_tt else {}
    picker = parse_picksta(args.picksta)
    opts = {'match_tol': args.match_tol, 'resid_tol': args.resid_tol,
            'max_weight': args.max_weight, 'pre_sec': args.pre_sec,
            'post_margin': args.post_margin}
    cfgq = parse_config_q(args.config)
    q = build_q(args, cfgq)
    mans = load_manifests(args.tanks) if args.tanks else []
    os.makedirs(args.out, exist_ok=True)
    good_dir = os.path.join(args.out, 'good_s')
    conf_dir = os.path.join(args.out, 'confiable')
    os.makedirs(good_dir, exist_ok=True)
    os.makedirs(conf_dir, exist_ok=True)

    all_rows, counters = [], []
    n_events_picks = 0
    n_no_tank = 0
    per_event = []
    for eid in sorted(events):
        ev = events[eid]
        if not in_range(eid, args.range):
            continue
        slug = map_event(eid, ev['t0'], mans) if args.tanks else 'test%d' % eid
        if args.only != '*' and not fnmatch.fnmatch(slug or ('test%d' % eid), args.only):
            continue
        if slug is None:
            per_event.append((eid, '-', 'sin_tank', 0, 0, 0, 0))
            n_no_tank += 1
            continue
        pf = find_file(args.picks_s, slug, '.picks')
        detected = parse_picks_text(open(pf).read()) if pf else []
        mf = find_file(args.metrics, slug, '.metrics') if args.metrics else None
        mets = parse_metrics(mf) if mf else None
        tsmax = 0.0
        for (teid, _sta), v in tt.items():
            if teid == eid and v['ts'] > 0 and v['ts'] > tsmax:
                tsmax = v['ts']
        if tsmax > 0:
            win = (ev['t0'] - opts['pre_sec'], ev['t0'] + tsmax + opts['post_margin'])
        else:
            win = (ev['t0'] - opts['pre_sec'], ev['t0'] + 300.0)
        rows, c = analyze_event(ev, picks.get(eid, []), detected, tt, picker, opts, win,
                                metrics=mets, q=q)
        for r in rows:
            r['slug'] = slug
        all_rows.extend(rows)
        counters.append(c)
        if rows:
            n_events_picks += 1
        per_event.append((eid, slug, 'ok' if pf else 'sin_captura',
                          len(rows), c['en_ref'], c['sin_ref'], c['nueva']))
        good = [r['raw'] for r in rows if r['bien']]
        if good:
            with open(os.path.join(good_dir, slug + '.picks'), 'w') as fh:
                fh.write("\n".join(good) + "\n")
        conf = [r['raw'] for r in rows if r['certeza'] == 'ALTA']
        if conf:
            with open(os.path.join(conf_dir, slug + '.picks'), 'w') as fh:
                fh.write("\n".join(conf) + "\n")

    all_rows.sort(key=lambda r: (r['event'], r['sta'], r['t']))
    summ = summarize(all_rows, counters, len(events), n_events_picks, n_no_tank, opts)

    # CSV
    with open(os.path.join(args.out, 'report.csv'), 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=CSV_COLS)
        w.writeheader()
        for r in all_rows:
            w.writerow({k: r[k] for k in CSV_COLS})
    with open(os.path.join(args.out, 'summary.json'), 'w') as fh:
        json.dump(summ, fh, indent=2, sort_keys=True)

    # Tabla por evento
    if args.format in ('table', 'all'):
        print("ev  slug      estado       det  REF SREF NUEVA")
        for (eid, slug, st, nd, en, sr, nu) in per_event:
            print("%-3d %-9s %-11s %4d %4d %4d %5d" % (eid, slug, st, nd, en, sr, nu))
    print()
    print("resumen: eventos=%d sin_tank=%d con_picks=%d detectadas=%d (fuera_ventana=%d)"
          % (summ['events_total'], summ['events_without_tank'], summ['events_with_picks'],
             summ['detected_S'], summ['out_of_window']))
    print("         EN_REF=%d SIN_REF=%d NUEVA=%d FN=%d BIEN=%d"
          % (summ['EN_REF'], summ['SIN_REF'], summ['NUEVA'], summ['FN'], summ['BIEN']))
    cz = summ['certeza']
    print("certeza (parametros): ALTA=%d BAJA=%d pureza_alta=%s"
          % (cz['alta'], cz['baja'], cz['pureza_alta']))
    fc = summ['file_check']
    print("chequeo suelto archivo: en_ref=%d dt_mediana=%s pct_dt<=2s=%s"
          % (fc['en_ref'], fc['dt_manual'].get('median'), fc['pct_dt_le_2s']))
    print("cobertura manual S: out=%d chan_diff=%d hh=%d (total=%d)"
          % (summ['coverage']['out_of_coverage'],
             summ['coverage']['name_covered_chan_diff'],
             summ['coverage']['hh_covered'], summ['manual_S_total']))
    print("resid_S: %s" % summ['resid_S'])
    print("salidas en %s" % args.out)


# --------------------------------------------------------------------------
# Calibración de umbrales intrínsecos
# --------------------------------------------------------------------------

def cmd_calibrate(args):
    events, picks = parse_dat(args.dat)
    tt = parse_tt_text(open(args.events_tt).read()) if args.events_tt else {}
    mans = load_manifests(args.tanks) if args.tanks else []
    cfgq = parse_config_q(args.config)
    q = build_q(args, cfgq)

    rows = []
    for eid in sorted(events):
        ev = events[eid]
        if not in_range(eid, args.range):
            continue
        slug = map_event(eid, ev['t0'], mans) if args.tanks else 'test%d' % eid
        if args.only != '*' and not fnmatch.fnmatch(slug or ('test%d' % eid), args.only):
            continue
        if slug is None:
            continue
        mf = find_file(args.metrics, slug, '.metrics')
        if not mf:
            continue
        mets = parse_metrics(mf)
        manual_s = [m for m in picks.get(eid, []) if m['phase'] == 'S']
        tsmax = 0.0
        for (teid, _sta), v in tt.items():
            if teid == eid and v['ts'] > 0 and v['ts'] > tsmax:
                tsmax = v['ts']
        win = (ev['t0'] - args.pre_sec,
               ev['t0'] + (tsmax if tsmax > 0 else 300.0) + args.post_margin)
        for m in mets:
            if not (win[0] <= m['t'] <= win[1]):
                continue
            mm, dtm = match_manual(manual_s, m['t'], args.match_tol, m['sta'])
            ttrow = tt.get((eid, m['sta']))
            resid = (m['t'] - (ev['t0'] + ttrow['ts'])) if (ttrow and ttrow['ts'] > 0) else None
            rows.append({'en_ref': 1 if mm else 0, 'keep': m['verdict'] == 'keep',
                         'weight': m['w'], 'resid': resid, 'dt_manual': dtm,
                         'snr': m['snr'], 'stalta': m['stalta'], 'rect': m['rect'],
                         'plan': m['plan'], 'inc': m['inc'], 'hv': m['hv'],
                         'dtsp': m['dtsp']})

    def rate(acc):
        n = len(acc)
        en = sum(r['en_ref'] for r in acc)
        return n, en, (100.0 * en / n if n else 0.0)

    sweeps = {}
    n0, en0, r0 = rate(rows)
    nk, enk, rk = rate([r for r in rows if r['keep']])
    print("[calibrate] detecciones (keep+drop) n=%d  en_ref=%d (%.1f%%)" % (n0, en0, r0))
    print("            keep (emitidas)      n=%d  en_ref=%d (%.1f%%)" % (nk, enk, rk))
    print()

    keep = [r for r in rows if r['keep']]
    print("-- sobre keep (emitidas) --")
    ksw = {}
    for mw in range(0, 5):
        n, en, rt = rate([r for r in keep if r['weight'] <= mw])
        ksw.setdefault('weight', []).append({'max_weight': mw, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   keep & weight<=%d   n=%-5d en_ref=%-5d (%.1f%%)" % (mw, n, en, rt))
    for thr in (5.0, 6.0, 8.0, 10.0, 12.0):
        n, en, rt = rate([r for r in keep if r['snr'] >= thr])
        ksw.setdefault('snr', []).append({'thr': thr, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   keep & snr>=%-4.1f    n=%-5d en_ref=%-5d (%.1f%%)" % (thr, n, en, rt))
    for thr in (0.5, 0.6, 0.7, 0.8, 0.9):
        n, en, rt = rate([r for r in keep if r['rect'] >= thr])
        ksw.setdefault('rect', []).append({'thr': thr, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   keep & rect>=%.1f    n=%-5d en_ref=%-5d (%.1f%%)" % (thr, n, en, rt))
    for lo, hi in ((60, 120), (65, 115), (70, 110)):
        n, en, rt = rate([r for r in keep if lo <= r['inc'] <= hi])
        ksw.setdefault('incid', []).append({'lo': lo, 'hi': hi, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   keep & inc[%3d,%3d] n=%-5d en_ref=%-5d (%.1f%%)" % (lo, hi, n, en, rt))
    for lo, hi in ((0.5, 20.0), (1.0, 40.0), (2.0, 30.0), (3.0, 25.0)):
        n, en, rt = rate([r for r in keep if lo <= r['dtsp'] <= hi])
        ksw.setdefault('dtsp', []).append({'lo': lo, 'hi': hi, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   keep & dtsp[%.1f,%.1f] n=%-5d en_ref=%-5d (%.1f%%)" % (lo, hi, n, en, rt))
    sweeps['keep'] = ksw
    print()

    for key in ('snr', 'stalta', 'rect', 'plan', 'hv'):
        vals = sorted(r[key] for r in rows)
        if not vals:
            continue
        qs = sorted(set(round(vals[min(len(vals) - 1, int(p * len(vals)))], 3)
                        for p in (0.1, 0.25, 0.5, 0.75, 0.9)))
        print("-- %s (mayor mejor) --" % key)
        lst = []
        for thr in qs:
            n, en, rt = rate([r for r in rows if r[key] >= thr])
            lst.append({'thr': thr, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
            print("   %-6s>=%-7.3f n=%-5d en_ref=%-5d (%.1f%%)" % (key, thr, n, en, rt))
        sweeps[key] = lst

    print("-- incidencia (ventana) --")
    lst = []
    for lo, hi in ((45, 135), (50, 130), (55, 125), (60, 120), (65, 115), (70, 110)):
        n, en, rt = rate([r for r in rows if lo <= r['inc'] <= hi])
        lst.append({'lo': lo, 'hi': hi, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   inc in [%3d,%3d] n=%-5d en_ref=%-5d (%.1f%%)" % (lo, hi, n, en, rt))
    sweeps['incid'] = lst

    print("-- weight (menor mejor) --")
    lst = []
    for mw in range(0, 5):
        n, en, rt = rate([r for r in rows if r['weight'] <= mw])
        lst.append({'max_weight': mw, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
        print("   weight<=%d    n=%-5d en_ref=%-5d (%.1f%%)" % (mw, n, en, rt))
    sweeps['weight'] = lst

    print("-- combinado snr/rect/inc --")
    lst = []
    for snr in (4.0, 5.0, 6.0, 7.0, 8.0):
        for rect in (0.5, 0.6, 0.7, 0.8):
            acc = [r for r in rows if r['snr'] >= snr and r['rect'] >= rect
                   and 60.0 <= r['inc'] <= 120.0]
            n, en, rt = rate(acc)
            if n >= args.min_n:
                lst.append({'snr': snr, 'rect': rect, 'n': n, 'en_ref': en, 'rate': round(rt, 1)})
                print("   snr>=%.1f rect>=%.1f inc[60,120] n=%-5d en_ref=%-5d (%.1f%%)"
                      % (snr, rect, n, en, rt))
    sweeps['combo'] = lst

    print("-- certeza con umbrales de %s --" % args.config)
    acc = [r for r in rows if certeza_of(r, q) == 'ALTA']
    n, en, rt = rate(acc)
    print("   ALTA n=%-5d en_ref=%-5d (%.1f%%)" % (n, en, rt))
    sweeps['certeza_config'] = {'n': n, 'en_ref': en, 'rate': round(rt, 1), 'q': q}

    if args.out:
        with open(args.out, 'w') as fh:
            json.dump({'base': {'n': n0, 'en_ref': en0, 'rate': round(r0, 1)},
                       'keep': {'n': nk, 'en_ref': enk, 'rate': round(rk, 1)},
                       'sweeps': sweeps}, fh, indent=2, sort_keys=True)
        print("[calibrate] json -> %s" % args.out)


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------

def selftest():
    fails = 0

    def check(cond, msg):
        nonlocal fails
        if cond:
            print("ok  : %s" % msg)
        else:
            print("FAIL: %s" % msg)
            fails += 1

    dat = ("1\t2026-01-01T00:00:00Z\t-30\t-71\t20\t4.0\tML\n"
           "C1.STA1..HHZ\t2026-01-01T00:00:10Z\tP\tmanual\n"
           "C1.STA1..HHN\t2026-01-01T00:00:20Z\tS\tmanual\n"
           "C1.STA2..BHN\t2026-01-01T00:00:21Z\tS\tmanual\n")
    events, picks = parse_dat_text(dat)
    check(len(events) == 1, "1 evento")
    check(sum(1 for p in picks[1] if p['phase'] == 'S') == 2, "2 S manuales")
    check(picks[1][0]['sta'] == 'STA1' and picks[1][0]['chan'] == 'HHZ', "SCNL parseado")

    pl = parse_picks_text("8 165 255 0 MT10.HHE.C1.-- ?2 20260101000020.000 5 0 0 S\n")
    check(len(pl) == 1 and pl[0]['sta'] == 'MT10' and pl[0]['chan'] == 'HHE'
          and pl[0]['net'] == 'C1' and pl[0]['loc'] == '--' and pl[0]['phase'] == 'S',
          "parse pickS STA.CHAN.NET.LOC")

    ml = parse_metrics_text(
        "pickS: METRICS sta=STA1 chan=HHE net=C1 loc=-- t=100.000 snr=8.00 stalta=8.00 "
        "rect=0.7000 plan=0.8000 inc=80.00 azi=10.00 hv=3.0000 dtsp=5.000 w=0 verdict=keep\n")
    check(len(ml) == 1 and ml[0]['snr'] == 8.0 and ml[0]['verdict'] == 'keep'
          and ml[0]['inc'] == 80.0, "parse METRICS")

    ev = events[1]
    manual = picks[1]
    tt = {(1, 'STA1'): {'delta': 1.0, 'tp': 10.0, 'ts': 20.0},
          (1, 'STA2'): {'delta': 1.2, 'tp': 11.0, 'ts': 21.0}}
    picker = {'STA1': {'HHE', 'HHN', 'HHZ'}, 'STA2': {'HHE', 'HHN', 'HHZ'}}
    opts = {'match_tol': 2.0, 'resid_tol': 2.5, 'max_weight': 3}

    def det(sta, chan, dt, w=1):
        return {'sta': sta, 'chan': chan, 'net': 'C1', 'loc': '--',
                't': ev['t0'] + dt, 'weight': w, 'phase': 'S', 'raw': 'r'}

    detected = [det('STA1', 'HHN', 20.0),      # EN_REF (manual STA1 HHN)
                det('STA2', 'HHN', 21.0),      # EN_REF por NOMBRE (manual STA2 BHN)
                det('STA1', 'HHE', 25.0)]      # SIN_REF (resid 5)
    rows, c = analyze_event(ev, manual, detected, tt, picker, opts)
    check(c['en_ref'] == 2, "EN_REF=2 (uno por nombre de estacion)")
    check(c['sin_ref'] == 1, "SIN_REF=1 (no es FP)")
    check(c['nueva'] == 0 and c['fn'] == 0, "NUEVA=0 FN=0")
    check(c['chan_diff'] == 1, "cobertura chan_diff=1 (BHN)")
    check(c['hh_covered'] == 1, "cobertura hh_covered=1")
    r2 = [r for r in rows if r['sta'] == 'STA2'][0]
    check(r2['matched'] == 'si' and r2['chan_manual'] == 'BHN', "match por nombre con BHN")
    check(all(r['verdict'] in ('EN_REF', 'SIN_REF') for r in rows), "verdict EN_REF/SIN_REF")

    # SIN_REF: deteccion sin S manual (no es FP)
    detected2 = [det('STA1', 'HHN', 30.0)]   # no manual -> SIN_REF
    _, c2 = analyze_event(ev, manual, detected2, tt, picker, opts)
    check(c2['sin_ref'] == 1, "sin manual -> SIN_REF")
    tt2 = {(1, 'STA1'): {'delta': 1.0, 'tp': 10.0, 'ts': 29.0}}
    _, c3 = analyze_event(ev, manual, detected2, tt2, picker, opts)
    check(c3['nueva'] == 1, "sin manual y resid bajo -> NUEVA (subset SIN_REF)")

    # certeza por parametros (independiente del archivo)
    q = {'snr': 4.0, 'stalta': 4.0, 'rect': 0.5, 'incid_min': 60.0, 'incid_max': 120.0,
         'plan': None, 'hv': None}
    mets = [{'sta': 'STA1', 't': ev['t0'] + 20.0, 'snr': 8.0, 'stalta': 8.0,
             'rect': 0.7, 'plan': 0.8, 'inc': 80.0, 'hv': 3.0, 'dtsp': 5.0},
            {'sta': 'STA2', 't': ev['t0'] + 21.0, 'snr': 2.0, 'stalta': 8.0,
             'rect': 0.7, 'plan': 0.8, 'inc': 80.0, 'hv': 3.0, 'dtsp': 5.0}]
    rows3, c3b = analyze_event(ev, manual, detected, tt, picker, opts, metrics=mets, q=q)
    check(c3b['alta'] == 1 and c3b['baja'] == 1, "certeza ALTA/BAJA por parametros")
    check(any(r['certeza'] == 'ALTA' for r in rows3), "columna certeza presente")

    # determinismo del CSV de resumen
    rows_sorted = sorted(rows, key=lambda r: (r['event'], r['sta'], r['t']))
    check([r['sta'] for r in rows_sorted] == sorted([r['sta'] for r in rows]),
          "orden determinista")

    if fails:
        print("\n%d FALLOS" % fails)
        return 1
    print("\nOK selftest picks_s_manual")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)

    pe = sub.add_parser('events', help='extrae eventos a TSV para ttp')
    pe.add_argument('--dat', required=True)
    pe.add_argument('--out', required=True)
    pe.add_argument('--only', default='*')
    pe.add_argument('--range', default='')
    pe.set_defaults(func=cmd_events)

    pr = sub.add_parser('report', help='reporte de validación')
    pr.add_argument('--dat', required=True)
    pr.add_argument('--tanks', default='')
    pr.add_argument('--picks-s', required=True)
    pr.add_argument('--events-tt', default='')
    pr.add_argument('--picksta', default='run_working_v8/params/pickS.sta')
    pr.add_argument('--match-tol', type=float, default=2.0)
    pr.add_argument('--resid-tol', type=float, default=2.5)
    pr.add_argument('--max-weight', type=int, default=4)
    pr.add_argument('--pre-sec', type=float, default=5.0)
    pr.add_argument('--post-margin', type=float, default=10.0)
    pr.add_argument('--metrics', default='')
    pr.add_argument('--config', default='run_working_v8/params/pickS.d')
    pr.add_argument('--q-snr', type=float, default=None)
    pr.add_argument('--q-stalta', type=float, default=None)
    pr.add_argument('--q-rect', type=float, default=None)
    pr.add_argument('--q-plan', type=float, default=None)
    pr.add_argument('--q-hv', type=float, default=None)
    pr.add_argument('--q-incid-min', type=float, default=None)
    pr.add_argument('--q-incid-max', type=float, default=None)
    pr.add_argument('--q-dtsp-min', type=float, default=None)
    pr.add_argument('--q-dtsp-max', type=float, default=None)
    pr.add_argument('--format', choices=['table', 'csv', 'json', 'all'], default='all')
    pr.add_argument('--only', default='*')
    pr.add_argument('--range', default='')
    pr.add_argument('--out', required=True)
    pr.set_defaults(func=cmd_report)

    pc = sub.add_parser('calibrate', help='barrido de umbrales intrinsecos (metricas)')
    pc.add_argument('--dat', required=True)
    pc.add_argument('--tanks', default='')
    pc.add_argument('--metrics', required=True)
    pc.add_argument('--events-tt', default='')
    pc.add_argument('--picksta', default='run_working_v8/params/pickS.sta')
    pc.add_argument('--config', default='run_working_v8/params/pickS.d')
    pc.add_argument('--match-tol', type=float, default=2.0)
    pc.add_argument('--pre-sec', type=float, default=5.0)
    pc.add_argument('--post-margin', type=float, default=10.0)
    pc.add_argument('--min-n', type=int, default=20)
    pc.add_argument('--q-snr', type=float, default=None)
    pc.add_argument('--q-stalta', type=float, default=None)
    pc.add_argument('--q-rect', type=float, default=None)
    pc.add_argument('--q-plan', type=float, default=None)
    pc.add_argument('--q-hv', type=float, default=None)
    pc.add_argument('--q-incid-min', type=float, default=None)
    pc.add_argument('--q-incid-max', type=float, default=None)
    pc.add_argument('--q-dtsp-min', type=float, default=None)
    pc.add_argument('--q-dtsp-max', type=float, default=None)
    pc.add_argument('--only', default='*')
    pc.add_argument('--range', default='')
    pc.add_argument('--out', default='')
    pc.set_defaults(func=cmd_calibrate)

    ps = sub.add_parser('selftest')
    ps.set_defaults(func=lambda a: selftest())

    args = ap.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
