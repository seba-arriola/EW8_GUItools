#!/usr/bin/env python3
"""
gen_guide_p.py - Genera ficheros de guía P para el modo offline de pickS a
partir de las picadas P manuales de picks_por_tests410.dat.

El modo guiado (GuideMode hybrid) de pickS solo usa el NOMBRE DE ESTACIÓN y la
fase P, así que se sintetiza una línea TYPE_PICK_SCNL por picada P:

    8 0 0 <seq> STA.CHAN.NET.LOC ?1 YYYYMMDDhhmmss.000 0 0 0 P

Los ficheros se escriben como <slug>.picks (slug = test<id>) en --out, que es
lo que espera `capture_picks_s.sh --ppicks`.

Uso:
  gen_guide_p.py --dat picks_por_tests410.dat --out picks_p_manual [--only GLOB]
  gen_guide_p.py --selftest
"""

import argparse
import fnmatch
import os
import sys


def split_scnl(scnl):
    p = scnl.strip().split('.')
    while len(p) < 4:
        p.append('')
    return p[0], p[1], p[2], p[3]


def iso_to_stamp(s):
    s = s.strip()
    if s.endswith('Z') or s.endswith('z'):
        s = s[:-1]
    date, time = s.split('T')
    y, mo, d = date.split('-')
    hh, mi, ss = time.split(':')
    return "%s%s%s%s%s%s" % (y, mo, d, hh, mi, ss)


def parse_dat(path):
    """Devuelve {id: [(net,sta,loc,chan,tstamp), ...]} solo de picadas P."""
    picks = {}
    cur = None
    with open(path, 'r') as fh:
        for raw in fh:
            line = raw.rstrip('\n')
            if not line.strip():
                continue
            f = [x.strip() for x in line.split('\t')]
            if len(f) >= 7 and f[0].isdigit() and 'T' in f[1]:
                cur = int(f[0])
                picks.setdefault(cur, [])
                continue
            if len(f) < 4 or cur is None:
                continue
            if f[2].upper() != 'P':
                continue
            net, sta, loc, chan = split_scnl(f[0])
            picks[cur].append((net, sta, loc, chan, iso_to_stamp(f[1])))
    return picks


def generate(dat_path, out_dir, only='*'):
    picks = parse_dat(dat_path)
    os.makedirs(out_dir, exist_ok=True)
    n_files = 0
    n_picks = 0
    for eid in sorted(picks):
        slug = 'test%d' % eid
        if not fnmatch.fnmatch(slug, only):
            continue
        rows = picks[eid]
        if not rows:
            continue
        seq = 0
        with open(os.path.join(out_dir, slug + '.picks'), 'w') as fh:
            for (net, sta, loc, chan, stamp) in rows:
                seq += 1
                loc = loc if loc else '--'
                fh.write("8 0 0 %d %s.%s.%s.%s ?1 %s.000 0 0 0 P\n"
                         % (seq, sta, chan, net, loc, stamp))
        n_files += 1
        n_picks += seq
    return n_files, n_picks


def selftest():
    import tempfile
    dat = ("1\t2026-01-01T00:00:00Z\t-30\t-71\t20\t4.0\tML\n"
           "C1.STA1..HHZ\t2026-01-01T00:00:10Z\tP\tmanual\n"
           "C1.STA1..HHN\t2026-01-01T00:00:20Z\tS\tmanual\n"
           "CX.STA2.08.BHZ\t2026-01-01T00:00:11Z\tP\tmanual\n")
    fails = 0
    with tempfile.TemporaryDirectory() as td:
        dpath = os.path.join(td, 'x.dat')
        with open(dpath, 'w') as fh:
            fh.write(dat)
        nf, npk = generate(dpath, os.path.join(td, 'out'))
        if nf == 1 and npk == 2:
            print("ok  : genera 1 fichero con 2 P")
        else:
            print("FAIL: nf=%d npk=%d" % (nf, npk)); fails += 1
        txt = open(os.path.join(td, 'out', 'test1.picks')).read()
        if 'STA1.HHZ.C1.--' in txt and 'STA2.BHZ.CX.08' in txt and txt.count(' P\n') == 2:
            print("ok  : formato STA.CHAN.NET.LOC + fase P")
        else:
            print("FAIL: formato"); print(txt); fails += 1
    if fails:
        print("\n%d FALLOS" % fails)
        return 1
    print("\nOK selftest gen_guide_p")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dat')
    ap.add_argument('--out')
    ap.add_argument('--only', default='*')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.dat or not args.out:
        ap.error('--dat y --out son obligatorios (o usa --selftest)')
    nf, npk = generate(args.dat, args.out, args.only)
    print("[guide] %d ficheros, %d picadas P -> %s" % (nf, npk, args.out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
