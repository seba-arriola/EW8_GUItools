#!/usr/bin/env python3
"""mk_sta.py - Convierte estaciones de csnloc al formato .sta de HYPOINVERSE.

Entrada (estaciones_107.txt de csnloc):
    <STA> <NET> <CHAN> <LOC> <lat> <lon> <elev_m> <cal>

Salida: tarjeta de estacion HYPOINVERSE-2000 de ancho fijo (82 col), formato
"new hypoinverse" (H71 ... 3):

    cols  1- 5  A5  codigo de estacion
    col   6     1X
    cols  7- 8  A2  red
    col   9     1X
    col  10     A1  componente (1 letra)
    cols 11-13  A3  componente (SEED)
    col  14     1X
    col  15     A1  peso (blanco = completo)
    cols 16-17  I2  latitud grados
    col  18     1X
    cols 19-25 F7.4 latitud minutos
    col  26     A1  N/S
    cols 27-29  I3  longitud grados
    col  30     1X
    cols 31-37 F7.4 longitud minutos
    col  38     A1  E/W
    cols 39-42  4X  elevacion (reservada)
    ...
    cols 81-82  A2  location code

Uso:
    python3 mk_sta.py estaciones_107.txt estaciones_hyp.sta

Por cada estacion se emiten TRES tarjetas: el canal tal cual viene del fichero
(el vertical, p. ej. HHZ) y sus dos horizontales derivados (HHN/HHE). HYPOINVERSE
busca la estacion por componente, asi que sin las tarjetas horizontales descarta
las S —que `pickS` pica justamente en HHN/HHE— con
"SKIP PHASE CARD WITH UNKNOWN STATION" y la S nunca entra a la solucion.
"""
import sys


def horizontals(chan):
    """Canales horizontales derivados del vertical (HHZ -> HHN, HHE)."""
    if not chan:
        return ()
    return (chan[:-1] + "N", chan[:-1] + "E")


def card(sta, net, chan, loc, lat, lon):
    c = [" "] * 82

    def put(col0, text):
        for i, ch in enumerate(text):
            if col0 + i < len(c):
                c[col0 + i] = ch

    put(0, sta[:5].ljust(5))
    put(6, net[:2])
    put(9, chan[:1])
    put(10, chan[:3])

    latd = int(abs(lat))
    latm = (abs(lat) - latd) * 60.0
    put(15, f"{latd:2d}")
    put(18, f"{latm:7.4f}")
    c[25] = "N" if lat >= 0 else "S"

    lond = int(abs(lon))
    lonm = (abs(lon) - lond) * 60.0
    put(26, f"{lond:3d}")
    put(30, f"{lonm:7.4f}")
    c[37] = "E" if lon >= 0 else "W"

    put(80, (loc if loc and loc != "--" else "  ")[:2])
    return "".join(c)


def main(argv):
    if len(argv) != 3:
        print("Uso: mk_sta.py <entrada> <salida>", file=sys.stderr)
        return 1
    n = 0
    seen = set()
    with open(argv[1], "r", encoding="utf-8", errors="replace") as fin, \
         open(argv[2], "w", encoding="utf-8") as fout:
        for line in fin:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if len(p) < 6:
                continue
            sta, net, chan = p[0], p[1], p[2]
            loc = p[3] if len(p) > 3 else "--"
            lat, lon = float(p[4]), float(p[5])
            for ch in (chan,) + horizontals(chan):
                if (sta, net, ch) in seen:
                    continue
                seen.add((sta, net, ch))
                fout.write(card(sta, net, ch, loc, lat, lon) + "\n")
                n += 1
    print(f"escrito: {argv[2]} ({n} tarjetas)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
