#!/usr/bin/env python3
"""arc_filter.py - Filtra lineas de fase de un conjunto de ARC (crudo de csnloc).

Sirve para aislar el efecto de una fase. El caso que motivo la herramienta:
`hyp2000` **no ve las S** porque `csnloc` las escribe en el campo del remark P
(`KPRK`, offsets 13-15) con los campos S (tiempo 41-45, `KSRK` 46-47, `LSWT` 49)
en blanco -> `hyphs.for:552` anula el remark S y `hyloc.for:211` nunca entra a
la rama S, mientras `hyloc.for:204` mete la linea por la rama P con peso lleno.
Borrar las lineas S de una copia de los ARC deja medir cuanto daña eso.

OJO: cada fase ocupa DOS lineas (la primaria y su continuacion, con `$1` en
[104:106]). Se descartan las dos, y el `nph` del header ([39:42]) se reescribe
para que el filtro `MinPhases` del anillo siga siendo coherente.

Uso:
    python3 tank_tools/arc_filter.py --in tmp/refine/arcs --out tmp/x/arcs \\
        --drop-phase S [--selftest]
"""
import argparse
import glob
import os
import sys

HEADER_MIN = 190
PHASE_LEN = 114


def filter_arc(text, drop):
    """Devuelve el ARC sin las fases de `drop` (set de letras)."""
    lines = text.split("\n")
    out, hdr_idx, nph, i = [], None, 0, 0

    # cabecera: primera linea de >= 190 chars
    while i < len(lines) and len(lines[i]) < HEADER_MIN:
        out.append(lines[i])
        i += 1
    if i < len(lines):
        hdr_idx = len(out)
        out.append(lines[i])
        i += 1

    while i < len(lines):
        line = lines[i]
        if len(line) >= PHASE_LEN and line[14] in ("P", "S") and line[14] in drop:
            i += 1
            if i < len(lines) and len(lines[i]) >= 106 \
                    and lines[i][104:106] == "$1":
                i += 1                      # se va tambien la continuacion
            continue
        if len(line) >= PHASE_LEN and line[14] in ("P", "S"):
            nph += 1
        out.append(line)
        i += 1

    if hdr_idx is not None and len(out[hdr_idx]) >= 42:
        h = out[hdr_idx]
        out[hdr_idx] = h[:39] + ("%3d" % nph) + h[42:]
    return "\n".join(out)


def main(argv):
    ap = argparse.ArgumentParser(add_help=False, description=__doc__.split("\n")[0])
    ap.add_argument("--in", dest="src", help="dir con los .arc de entrada")
    ap.add_argument("--out", dest="dst", help="dir de salida (se crea)")
    ap.add_argument("--drop-phase", default="",
                    help="letras de fase a descartar (p.ej. 'S' o 'P,S')")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("-h", "--help", action="store_true")
    a = ap.parse_args(argv[1:])

    if a.selftest:
        return selftest()
    if a.help or not (a.src and a.dst):
        print(__doc__.strip())
        return 0

    drop = set(c for c in a.drop_phase.replace(",", "").strip() if c in "PS")
    os.makedirs(a.dst, exist_ok=True)
    n_in = n_out = 0
    for p in sorted(glob.glob(os.path.join(a.src, "*.arc"))):
        with open(p, "r", encoding="latin-1", errors="replace") as fh:
            txt = fh.read()
        with open(os.path.join(a.dst, os.path.basename(p)), "w",
                  encoding="latin-1") as fh:
            fh.write(filter_arc(txt, drop))
        n_in += 1
        n_out += 1
    print("arc_filter: %d ARC -> %s (descartando fases %s)"
          % (n_out, a.dst, "".join(sorted(drop)) or "-"))
    return 0


def selftest():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import refine_arcs

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    arc = refine_arcs._mk_arc(
        2026, 7, 22, 2, 3, 4900, -19.36, -70.3185, 2750, 5, 178, 33, 121,
        [("PSGCX", "CX", "HHZ", "P"), ("PB23", "CX", "HHZ", "P"),
         ("TA02", "C1", "HHZ", "S"), ("CO02", "C1", "HHZ", "S"),
         ("MT02", "C1", "HHZ", "P")])
    h = refine_arcs.arc_header(arc)
    check(h["nphases"] == 5, "sintetico: 5 fases (nph=%d)" % h["nphases"])
    check(len(refine_arcs.arc_phases(arc)) == 5, "sintetico: 5 lineas primarias")

    sin_s = filter_arc(arc, set("S"))
    ph = refine_arcs.arc_phases(sin_s)
    check(len(ph) == 3, "drop S: quedan 3 primarias (%d)" % len(ph))
    check(all(p["phase"] == "P" for p in ph), "drop S: ninguna S")
    check(refine_arcs.arc_header(sin_s)["nphases"] == 3,
          "drop S: nph del header reescrito a 3")
    check("$1" in sin_s.split("\n")[1], "drop S: la cabecera sigue con $1")

    sin_p = filter_arc(arc, set("P"))
    check(len(refine_arcs.arc_phases(sin_p)) == 2, "drop P: quedan 2")
    check(refine_arcs.arc_header(sin_p)["nphases"] == 2, "drop P: nph=2")

    vacio = filter_arc(arc, set("PS"))
    check(len(refine_arcs.arc_phases(vacio)) == 0, "drop P y S: 0 fases")
    check(refine_arcs.arc_header(vacio)["nphases"] == 0, "drop P y S: nph=0")
    check(refine_arcs.arc_header(vacio)["lat"] is not None,
          "drop P y S: la cabecera sigue siendo parseable")

    igual = filter_arc(arc, set())
    check(refine_arcs.arc_header(igual)["nphases"] == 5,
          "drop vacio: no cambia nada")

    print()
    print("%s selftest arc_filter" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
