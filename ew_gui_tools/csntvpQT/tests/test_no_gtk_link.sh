#!/bin/bash
# El binario Qt no debe arrastrar GTK/libadwaita (arrastraria el mundo GTK).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$ROOT/ew_gui_tools/csntvpQT/csntvpQT"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

if [ ! -x "$BIN" ]; then
    bad "falta $BIN (compila antes de este test)"
    exit 1
fi

if ldd "$BIN" 2>/dev/null | grep -qi 'libgtk'; then
    bad "el binario enlaza libgtk"
else
    ok "sin libgtk"
fi
if ldd "$BIN" 2>/dev/null | grep -qi 'libadwaita'; then
    bad "el binario enlaza libadwaita"
else
    ok "sin libadwaita"
fi
if ldd "$BIN" 2>/dev/null | grep -qi 'libQt6Widgets'; then
    ok "enlaza Qt6Widgets"
else
    bad "no enlaza Qt6Widgets"
fi
if ldd "$BIN" 2>/dev/null | grep -qi 'libcairo'; then
    ok "enlaza cairo (la capa C dibuja)"
else
    bad "no enlaza cairo"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: el binario Qt no arrastra GTK"
fi
exit "$fail"
