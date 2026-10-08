#!/usr/bin/env python3
"""
merge_picks.py - combina archivos .picks (fases P y/o S) de forma idempotente.

Una linea .picks es un TYPE_PICK_SCNL:

    8 <mod> <inst> <seq> STA.CHAN.NET.LOC <fm><w> YYYYMMDDhhmmss.mmm <amp> 0 0 [fase]

La fase es el 11.o token opcional: 'S'/'s' => S; ausente o cualquier otra => P
(misma regla que csnloc: ew_gui_tools/csnloc/pick_scln.c:109-119).

El merge reconoce si un pick ya esta en el destino (clave SCNL + fase + tiempo)
y lo OMITE, asi que re-ejecutarlo no duplica nada. La salida queda ordenada por
tiempo (csnloc ya ordena internamente; aqui es solo por determinismo).

Uso:
    merge_picks.py [opciones] <fuente> [<fuente> ...]
    merge_picks.py --selftest

    <fuente>: directorio con <slug>.picks  o  un archivo .picks

Opciones:
    --out DIR        destino (obligatorio): <out>/<slug>.picks
    --phases LIST    P | S | PS  (default PS)   <- "solo las S" / "solo las P"
    --only GLOB      procesar solo slugs que casen (default *)
    --force          reconstruye desde las fuentes (ignora el contenido del destino).
                     NO lo uses con --out dentro de una fuente y una sola fase
                     (p. ej. --phases S sobre un dir P): borraria la otra fase.
    --dry-run        no escribe; informa +anadidos/omitidos por slug
    --quiet          sin salida por slug
    -h, --help       esta ayuda

Salida por slug:
    [merge] <slug>: +N (P=a S=b) omit=M -> <out>/<slug>.picks
    [merge] <slug>: sin cambios (SKIP)

Codigos: 0 ok | 1 error de uso o sin fuentes
"""

import argparse
import fnmatch
import os
import re
import shutil
import sys
import tempfile

TIME_RE = re.compile(r'^\d{14}\.\d+$')


def parse_pick_line(line):
    """Linea .picks -> dict {line, scnl, phase, tstr}; None si malformada."""
    f = line.split()
    if len(f) < 7 or not TIME_RE.match(f[6]):
        return None
    phase = 'S' if (len(f) >= 11 and f[10][:1].upper() == 'S') else 'P'
    return {'line': line, 'scnl': f[4], 'phase': phase, 'tstr': f[6]}


def pick_key(rec):
    """Clave de deduplicacion: la fase entra, asi P y S coexisten."""
    return (rec['scnl'], rec['phase'], rec['tstr'])


def load_picks(path):
    recs, bad = [], 0
    with open(path, 'r') as fh:
        for raw in fh:
            s = raw.rstrip('\n')
            if not s.strip():
                continue
            rec = parse_pick_line(s)
            if rec is None:
                bad += 1
                continue
            recs.append(rec)
    return recs, bad


def read_target(path):
    """Devuelve (recs validos, lineas no parseables a preservar)."""
    if not os.path.isfile(path):
        return [], []
    recs, passthrough = [], []
    with open(path, 'r') as fh:
        for raw in fh:
            s = raw.rstrip('\n')
            if not s.strip():
                continue
            rec = parse_pick_line(s)
            if rec is None:
                passthrough.append(s)
            else:
                recs.append(rec)
    return recs, passthrough


def source_files(sources, only):
    """slug -> [paths] en el orden de las fuentes (dirs con <slug>.picks o un .picks)."""
    found = {}
    for src in sources:
        if os.path.isdir(src):
            for n in sorted(os.listdir(src)):
                if not n.endswith('.picks'):
                    continue
                slug = n[:-len('.picks')]
                if not fnmatch.fnmatch(slug, only):
                    continue
                found.setdefault(slug, []).append(os.path.join(src, n))
        elif os.path.isfile(src) and src.endswith('.picks'):
            slug = os.path.basename(src)[:-len('.picks')]
            if fnmatch.fnmatch(slug, only):
                found.setdefault(slug, []).append(src)
    return found


def merge_slug(existing, passthrough, src_paths, phases, force):
    """Fusiona el destino con las fuentes. Devuelve (lines, added, omitted, bad)."""
    keys = set()
    lines = []
    if not force:
        for rec in existing:
            keys.add(pick_key(rec))
            lines.append(rec)
    added = {'P': 0, 'S': 0}
    omitted = 0
    bad = 0
    for path in src_paths:
        recs, b = load_picks(path)
        bad += b
        for rec in recs:
            if rec['phase'] not in phases:
                continue
            k = pick_key(rec)
            if k in keys:
                omitted += 1
                continue
            keys.add(k)
            lines.append(rec)
            added[rec['phase']] += 1
    lines.sort(key=lambda r: (r['tstr'], r['scnl'], r['phase']))
    out_lines = [r['line'] for r in lines]
    if not force:
        out_lines += passthrough
    return out_lines, added, omitted, bad


def parse_phases(text):
    ph = set(ch for ch in (text or '').upper() if ch in 'PS')
    if not ph:
        raise ValueError("--phases debe ser P, S o PS")
    return ph


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('--out')
    ap.add_argument('--phases', default='PS')
    ap.add_argument('--only', default='*')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--dry-run', dest='dry_run', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('-h', '--help', action='store_true')
    ap.add_argument('sources', nargs='*')
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.help:
        print(__doc__.strip())
        return 0
    try:
        phases = parse_phases(args.phases)
    except ValueError as exc:
        print("[merge] ERROR: %s" % exc, file=sys.stderr)
        return 1
    if not args.out or not args.sources:
        print("[merge] ERROR: se requiere --out DIR y al menos una <fuente>", file=sys.stderr)
        return 1

    src = source_files(args.sources, args.only)
    if not src:
        print("[merge] ERROR: no encontre .picks en %s" % args.sources, file=sys.stderr)
        return 1

    if not args.dry_run:
        os.makedirs(args.out, exist_ok=True)

    n_upd = n_skip = 0
    for slug in sorted(src):
        target = os.path.join(args.out, slug + '.picks')
        if args.force:
            existing, passthrough = [], []
        else:
            existing, passthrough = read_target(target)
        lines, added, omitted, bad = merge_slug(
            existing, passthrough, src[slug], phases, args.force)
        new_content = "".join(l + "\n" for l in lines)

        if args.force:
            changed = True
        else:
            old_content = None
            if os.path.isfile(target):
                with open(target, 'r') as fh:
                    old_content = fh.read()
            changed = (old_content != new_content)

        ntot = added['P'] + added['S']
        if args.dry_run:
            if not args.quiet:
                print("[merge] %s: DRY +%d (P=%d S=%d) omit=%d%s"
                      % (slug, ntot, added['P'], added['S'], omitted,
                         "" if changed else " (sin cambios)"))
            continue
        if changed:
            with open(target, 'w') as fh:
                fh.write(new_content)
            n_upd += 1
            if not args.quiet:
                extra = (" bad=%d" % bad) if bad else ""
                print("[merge] %s: +%d (P=%d S=%d) omit=%d%s -> %s"
                      % (slug, ntot, added['P'], added['S'], omitted, extra, target))
        else:
            n_skip += 1
            if not args.quiet:
                print("[merge] %s: sin cambios (SKIP)" % slug)

    if not args.quiet:
        print("[merge] listo: %d actualizados, %d sin cambios -> %s"
              % (n_upd, n_skip, args.out))
    return 0


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------

def selftest():
    d = tempfile.mkdtemp(prefix='merge_picks_selftest_')
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    def is_s(line):
        f = line.split()
        return len(f) >= 11 and f[10] == 'S'

    pdir = os.path.join(d, 'p')
    sdir = os.path.join(d, 's')
    os.makedirs(pdir)
    os.makedirs(sdir)
    P1 = "8 151 255 1 AAA.HHZ.C1.-- ?1 20260101000010.000 10 0 0"
    P2 = "8 151 255 2 BBB.HHZ.C1.-- ?1 20260101000030.000 12 0 0"
    S1 = "8 165 255 3 AAA.HHN.C1.-- ?2 20260101000016.000 11 0 0 S"
    S2 = "8 165 255 4 CCC.HHN.C1.-- ?2 20260101000020.000 13 0 0 S"
    with open(os.path.join(pdir, 'test1.picks'), 'w') as fh:
        fh.write(P1 + "\n" + P2 + "\n")
    with open(os.path.join(sdir, 'test1.picks'), 'w') as fh:
        fh.write(S1 + "\n" + S2 + "\n")
    with open(os.path.join(pdir, 'test2.picks'), 'w') as fh:
        fh.write(P1 + "\n")

    check(parse_pick_line(S1)['phase'] == 'S', "parse fase S (11.o token)")
    check(parse_pick_line(P1)['phase'] == 'P', "parse fase P (sin token)")

    # A1: combina P+S y ordena por tiempo
    out1 = os.path.join(d, 'out1')
    rc = main(['--out', out1, '--quiet', pdir, sdir])
    c = open(os.path.join(out1, 'test1.picks')).read().splitlines()
    ts = [l.split()[6] for l in c]
    check(rc == 0 and len(c) == 4, "A1 combina P+S (4 lineas)")
    check(ts == sorted(ts), "A1 ordenado por tiempo")
    check(sum(1 for l in c if is_s(l)) == 2, "A1 conserva las 2 S")

    # A2: idempotencia byte a byte
    before = open(os.path.join(out1, 'test1.picks')).read()
    main(['--out', out1, '--quiet', pdir, sdir])
    after = open(os.path.join(out1, 'test1.picks')).read()
    check(before == after, "A2 idempotente (sin duplicar)")

    # A3: solo P / solo S
    outp = os.path.join(d, 'outp')
    main(['--out', outp, '--phases', 'P', '--quiet', pdir, sdir])
    cp = open(os.path.join(outp, 'test1.picks')).read().splitlines()
    check(len(cp) == 2 and all(not is_s(l) for l in cp), "A3 --phases P")
    outs = os.path.join(d, 'outs')
    main(['--out', outs, '--phases', 'S', '--quiet', pdir, sdir])
    cs = open(os.path.join(outs, 'test1.picks')).read().splitlines()
    check(len(cs) == 2 and all(is_s(l) for l in cs), "A3 --phases S")

    # A4: dry-run no escribe
    outd = os.path.join(d, 'outd')
    main(['--out', outd, '--dry-run', '--quiet', pdir, sdir])
    check(not os.path.exists(outd), "A4 --dry-run no escribe")

    # A5: --force reconstruye desde las fuentes
    t1 = os.path.join(out1, 'test1.picks')
    with open(t1, 'w') as fh:
        fh.write("linea basura\n")
    main(['--out', out1, '--force', '--quiet', pdir, sdir])
    check(len(open(t1).read().splitlines()) == 4, "A5 --force reconstruye")

    # A7: resume parcial (agregar S despues de P, sin duplicar)
    out7 = os.path.join(d, 'out7')
    main(['--out', out7, '--phases', 'P', '--quiet', pdir, sdir])
    main(['--out', out7, '--phases', 'S', '--quiet', pdir, sdir])
    c7 = open(os.path.join(out7, 'test1.picks')).read().splitlines()
    check(len(c7) == 4, "A7 resume: P + S sin duplicar")

    # A8: --only filtra slugs
    out8 = os.path.join(d, 'out8')
    main(['--out', out8, '--only', 'test1', '--quiet', pdir, sdir])
    check(os.path.exists(os.path.join(out8, 'test1.picks'))
          and not os.path.exists(os.path.join(out8, 'test2.picks')), "A8 --only filtra")

    shutil.rmtree(d, ignore_errors=True)
    print()
    print("%s selftest merge_picks" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
