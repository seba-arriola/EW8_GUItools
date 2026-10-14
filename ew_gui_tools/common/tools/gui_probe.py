#!/usr/bin/env python3
"""gui_probe.py - sonda AT-SPI para validar la GUI de los modulos GTK4.

Cliente EXTERNO: no necesita cambios en el codigo del modulo (GTK4 ya expone el
arbol de accesibilidad con GTK_A11Y=atspi, que es el default en Linux). Tiene que
correr dentro de la MISMA sesion D-Bus que la aplicacion: usar gui_run.sh.

Acciones (se ejecutan en este orden si se combinan):
  --click-by-name NAME   espera NAME y lo pulsa por su accion AT-SPI
  --rect NAME            imprime "x y w h" en coordenadas de pantalla
  --headers              lista los encabezados de columna (nombre + rect)
  --rows                 imprime una linea por fila de tabla con el texto de sus celdas
  --dump                 vuelca el arbol accesible (rol, nombre, rect)
  --focused              imprime "rol|nombre x y w h" del objeto con el foco
  --assert-text TEXTO    falla si ningun objeto contiene TEXTO en su nombre
  --assert-desc TEXTO    falla si ningun objeto lo lleva en su descripcion accesible
  --descs                lista rol|nombre|descripcion de los objetos con descripcion

Opciones: --under NAME (limita la busqueda al subarbol de ese objeto),
--timeout S (espera maxima, 15 s), --limit N (filas o profundidad del volcado, 40).

Geometria: GTK4 NO traduce las posiciones de los widgets a coordenadas de pantalla
por AT-SPI (todos los objetos reportan 0,0), pero SI reporta tamanos correctos y los
hijos en orden visual. La posicion se acumula sumando los tamanos de los hermanos
anteriores, y la posicion real del toplevel se toma de X (xdotool getwindowgeometry).

Salida: 0 = ok, 1 = asercion fallida, 2 = uso incorrecto, 3 = sin pyatspi.
"""
import argparse
import subprocess
import sys
import time

try:
    import pyatspi
except Exception as exc:                                    # pragma: no cover
    sys.stderr.write("gui_probe: no pude importar pyatspi: %s\n" % exc)
    sys.stderr.write("gui_probe: instala python3-pyatspi y correlo dentro de "
                     "un bus de sesion (gui_run.sh)\n")
    sys.exit(3)

MAX_NODES = 20000
# 24 y no 16: las pestañas anidadas (Procesar > picking/magnitudes) suman niveles
# y con 16 el contenido de la sub-pestaña quedaba fuera del arbol.
MAX_DEPTH = 24


def _name(obj):
    try:
        return obj.name or ""
    except Exception:
        return ""


def _text(obj):
    """Contenido de un widget de texto (GtkEntry): en AT-SPI va en la interfaz
    Text, no en el nombre, que suele ir vacio."""
    try:
        t = obj.queryText()
        if t:
            return t.getText(0, -1) or ""
    except Exception:
        pass
    return ""


def _label(obj):
    """Como se identifica un objeto para la sonda: nombre y, si no hay, texto."""
    return _name(obj) or _text(obj)


def _desc(obj):
    """Descripción accesible (GtkAccessible). La usan los canvas de mapa, cuyas
    etiquetas de estado están ocultas: GTK4 no las expone por nombre."""
    try:
        return obj.description or ""
    except Exception:
        return ""


def _role(obj):
    try:
        return (obj.getRoleName() or "").lower()
    except Exception:
        return ""


def _size(obj):
    """(w, h) del widget o None. pyatspi 2.46 devuelve TUPLAS; otras versiones
    objetos con .x/.y: se aceptan las dos formas."""
    try:
        comp = obj.queryComponent()
        if not comp:
            return None
        s = comp.getSize()
        try:
            return (int(s[0]), int(s[1]))
        except TypeError:
            return (int(s.x), int(s.y))
    except Exception:
        return None


def _children(obj):
    out = []
    try:
        for i in range(obj.childCount):
            try:
                child = obj.getChildAtIndex(i)
            except Exception:
                child = None
            if child is not None:
                out.append(child)
    except Exception:
        pass
    return out


_FRAME_POS = {}


def _xdotool_name(title):
    """Escapa el titulo para el --name (regex) de xdotool."""
    out = []
    for ch in title:
        if ch in "\\.*+?()[]{}|^$":
            out.append("\\")
        out.append(ch)
    return "".join(out)


def _frame_pos(title):
    """Posicion real del toplevel en pantalla, leida de X (no de AT-SPI)."""
    if not title:
        return (0, 0)
    if title in _FRAME_POS:
        return _FRAME_POS[title]
    pos = (0, 0)
    try:
        out = subprocess.run(
            ["xdotool", "search", "--name", _xdotool_name(title),
             "getwindowgeometry", "--shell"],
            capture_output=True, text=True, timeout=5).stdout
        for line in out.splitlines():
            if line.startswith("X="):
                pos = (int(line[2:]), pos[1])
            elif line.startswith("Y="):
                pos = (pos[0], int(line[2:]))
    except Exception:
        pos = (0, 0)
    _FRAME_POS[title] = pos
    return pos


def walk(root=None, max_depth=MAX_DEPTH):
    """Genera (obj, depth, x, y) en orden de documento, con x,y de pantalla.

    Acumula tamanos de hermanos previos: en una caja vertical todos los hijos
    tienen el ancho del padre (desplazan en y), en una fila horizontal todos
    tienen su alto (desplazan en x). Si el contenedor no deja claro el eje se
    usa el criterio de que los anchos quepan.
    """
    start = root if root is not None else pyatspi.Registry.getDesktop(0)
    if _role(start) == "frame":
        fx, fy = _frame_pos(_name(start))
    else:
        fx, fy = 0, 0
    stack = [(start, 0, fx, fy)]
    seen = 0
    while stack:
        obj, depth, ox, oy = stack.pop()
        yield obj, depth, ox, oy
        seen += 1
        if seen > MAX_NODES or depth >= max_depth:
            continue
        kids = _children(obj)
        if not kids:
            continue
        cw, ch = _size(obj) or (0, 0)
        sizes = [_size(k) or (0, 0) for k in kids]
        nz = [s for s in sizes if s[0] > 0 or s[1] > 0]
        vertical = bool(nz) and all(w >= cw for w, _h in nz)
        horizontal = bool(nz) and all(h >= ch for _w, h in nz)
        sw = sh = 0
        pending = []
        for k, (w, h) in zip(kids, sizes):
            if vertical and not horizontal:
                dx, dy = 0, sh
            elif horizontal and not vertical:
                dx, dy = sw, 0
            else:
                dx, dy = (0, sh) if (sw + w) > cw else (sw, 0)
            if _role(k) == "frame":
                fx2, fy2 = _frame_pos(_name(k))
                nx, ny = ox + dx + fx2, oy + dy + fy2
            else:
                nx, ny = ox + dx, oy + dy
            pending.append((k, depth + 1, nx, ny))
            sw += w
            sh += h
        # La pila se recorre con pop(): se apilan al reves para emitir en orden
        # de documento (el orden visual, que es el que usan --rows/--dump).
        stack.extend(reversed(pending))


def find_all(name, root=None, exact=True):
    out = []
    for obj, _d, _x, _y in walk(root):
        nm = _label(obj)
        if (nm == name) if exact else (name in nm):
            out.append(obj)
    return out


def wait_for(name, timeout, exact=True):
    t0 = time.time()
    while True:
        got = find_all(name, exact=exact)
        if got:
            return got
        if time.time() - t0 >= timeout:
            return []
        time.sleep(0.2)


def find_all_desc(text, root=None):
    """Objetos cuya descripción accesible contiene `text`."""
    out = []
    for obj, _d, _x, _y in walk(root):
        if text in _desc(obj):
            out.append(obj)
    return out


def wait_for_desc(text, timeout):
    t0 = time.time()
    while True:
        got = find_all_desc(text)
        if got:
            return got
        if time.time() - t0 >= timeout:
            return []
        time.sleep(0.2)


def find_focused():
    """(obj, x, y) del primer objeto con el estado FOCUSED. Un GtkWindow tambien
    lo expone, asi que se prefiere un widget y solo se devuelve el frame si no hay
    nada mas (en el arnes sirve para saber donde esta el foco antes de mandar
    teclas: pulsar a ciegas puede activar el boton equivocado)."""
    fallback = None
    for obj, _d, x, y in walk():
        try:
            if not obj.getState().contains(pyatspi.STATE.FOCUSED):
                continue
        except Exception:
            continue
        if _role(obj) in ("frame", "window", "application", "desktop frame"):
            if fallback is None:
                fallback = (obj, x, y)
            continue
        return (obj, x, y)
    return fallback


def rect_of(name, root=None):
    """(x, y, w, h) del primer objeto con ese nombre, o None."""
    for obj, _d, x, y in walk(root):
        if _name(obj) == name:
            s = _size(obj)
            if s:
                return (x, y, s[0], s[1])
    return None


def click(obj):
    try:
        act = obj.queryAction()
    except Exception:
        act = None
    if not act:
        return False
    for i in range(act.nActions):
        try:
            if act.doAction(i):
                return True
        except Exception:
            continue
    return False


def texts_of(obj, skip_own=True):
    own = _label(obj)
    out = []
    for sub, _d, _x, _y in walk(obj, max_depth=6):
        if skip_own and sub is obj:
            continue
        nm = _label(sub).strip()
        if nm and nm != own and nm not in out:
            out.append(nm)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description="Sonda AT-SPI (ver cabecera del archivo)")
    ap.add_argument("--under", metavar="NAME")
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--headers", action="store_true")
    ap.add_argument("--rows", action="store_true")
    ap.add_argument("--rect", metavar="NAME")
    ap.add_argument("--focused", action="store_true")
    ap.add_argument("--click-by-name", metavar="NAME")
    ap.add_argument("--assert-text", metavar="TEXT")
    ap.add_argument("--assert-desc", metavar="TEXT")
    ap.add_argument("--descs", action="store_true")
    a = ap.parse_args(argv)

    if not any([a.dump, a.headers, a.rows, a.rect, a.focused, a.click_by_name,
                a.assert_text, a.assert_desc, a.descs]):
        ap.print_help()
        return 2

    root = None
    if a.under:
        got = wait_for(a.under, a.timeout)
        if not got:
            print("gui_probe: FALLO, no encontre el objeto de scope %r" % a.under)
            return 1
        root = got[0]

    rc = 0

    if a.click_by_name:
        cands = wait_for(a.click_by_name, a.timeout)
        if not cands:
            print("gui_probe: FALLO, no aparecio %r" % a.click_by_name)
            return 1
        if any(click(o) for o in cands):
            print("gui_probe: OK, pulse %r (%d candidato(s))" % (a.click_by_name, len(cands)))
        else:
            print("gui_probe: FALLO, %r no expone accion AT-SPI (%d candidato(s))"
                  % (a.click_by_name, len(cands)))
            return 1

    if a.rect:
        r = None
        for _i in range(int(max(a.timeout, 0.1) * 5)):
            r = rect_of(a.rect, root)
            if r:
                break
            time.sleep(0.2)
        if not r:
            print("gui_probe: FALLO, sin geometria para %r" % a.rect)
            return 1
        print("%d %d %d %d" % r)

    if a.focused:
        got = None
        for _i in range(int(max(a.timeout, 0.1) * 5)):
            got = find_focused()
            if got:
                break
            time.sleep(0.2)
        if not got:
            print("gui_probe: FALLO, ningun objeto con el foco")
            return 1
        o, x, y = got
        s = _size(o) or (0, 0)
        print("%s|%s %d %d %d %d" % (_role(o), _label(o), x, y, s[0], s[1]))

    if a.headers:
        hs = [(o, x, y) for o, _d, x, y in walk(root)
              if "header" in _role(o) and _name(o).strip()]
        for o, x, y in hs[:a.limit]:
            s = _size(o) or (0, 0)
            print("%s\t%d %d %d %d" % (_name(o), x, y, s[0], s[1]))
        if not hs:
            print("gui_probe: FALLO, sin encabezados de columna")
            rc = 1

    if a.rows:
        grids = [o for o, _d, _x, _y in walk(root)
                 if "grid" in _role(o) or "table" in _role(o)]
        if not grids:
            print("gui_probe: FALLO, sin tabla (grid) en el arbol")
            rc = 1
        for gi, g in enumerate(grids):
            print("# grid %d" % gi)
            n = 0
            for o, _d, _x, _y in walk(g):
                rn = _role(o)
                if "row" in rn and "header" not in rn:
                    print("%d\t%s" % (n, " | ".join(texts_of(o))))
                    n += 1
                    if n >= a.limit:
                        break
            if n == 0:
                print("gui_probe: FALLO, el grid %d no expone filas" % gi)
                rc = 1

    if a.dump:
        for o, d, x, y in walk(root):
            if d > a.limit:
                continue
            s = _size(o) or (0, 0)
            print("%s[%s] %s %d,%d %dx%d" % ("  " * d, _role(o), _name(o), x, y, s[0], s[1]))

    if a.assert_text:
        got = wait_for(a.assert_text, a.timeout, exact=False)
        if got:
            print("gui_probe: OK, %r presente (%d objeto(s))" % (a.assert_text, len(got)))
        else:
            print("gui_probe: FALLO, %r no aparece" % a.assert_text)
            rc = 1

    if a.descs:
        for o, _d, x, y in walk(root):
            d = _desc(o)
            if d:
                print("%s|%s|%s %d,%d" % (_role(o), _name(o), d, x, y))

    if a.assert_desc:
        got = wait_for_desc(a.assert_desc, a.timeout)
        if got:
            print("gui_probe: OK, %r presente en %d descripcion(es)" % (a.assert_desc, len(got)))
        else:
            print("gui_probe: FALLO, %r no aparece en ninguna descripcion" % a.assert_desc)
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
