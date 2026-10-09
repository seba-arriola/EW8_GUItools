#!/bin/bash
# =============================================================================
#  ew8_gtk_env.sh - entorno GTK4 de las GUI (WSLg)
#
#  FUENTE UNICA. No contiene rutas del proyecto ni variables de EarthWorm: solo
#  los dos `export` que necesitan los visores GTK4. Lo hacen `source` tanto el
#  `ew8_unix.sh` del repo como el del portable (deploy_portable.sh lo copia tal
#  cual junto al env generado).
#
#  Por que existe: `deploy_portable.sh` REGENERA `ew8_unix.sh` y no copiaba este
#  bloque, asi que cada deploy borraba las dos variables en silencio (incidente
#  2026-10-08: volvieron los avisos de libEGL/MESA y cambio la decoracion de las
#  ventanas). Con una sola copia no hay forma de que divergan.
#
#  Ver docs/GTK4-MIGRATION.md seccion 11.1 y docs/adr-0001-render-backend.md.
# =============================================================================

# GTK4: en WSL el backend GL/EGL (Vulkan/NGL) no inicializa y GTK4 cae a cairo
# con avisos de libEGL/MESA ("ZINK: failed to choose pdev", "failed to create
# dri2 screen"). Forzamos el renderer cairo (que es el que usa el canvas de los
# visores) para silenciarlos. OJO: cairo rasteriza en CPU; en una maquina con
# GPU real es mas lento que el renderer por defecto (ver ADR-0001).
export GSK_RENDERER=cairo

# GTK4 en WSLg: en WSLg antiguos (weston 9) los popovers (GtkDropDown y el menu
# del headerbar) se descolocaban, desaparecian al clic o saltaban de sitio
# (grab/posicion de xdg_popup roto; microsoft/wslg#1226, #1390). Por eso se
# forzaba XWayland (X11). Contra: bajo x11 la CSD de GTK no tiene zona de
# arrastre alcanzable en WSLg/RAIL y las ventanas NO se pueden redimensionar
# (ni aparecen flechitas; GTK_CSD=0/SSD tampoco lo arregla).
#
# Verificado 2026-10-09 en WSLg 1.0.73.2 (weston 04d436c): los popovers YA
# funcionan en Wayland y Wayland si permite redimensionar (el compositor
# decora). Por eso se deja el backend por defecto: Wayland si hay
# WAYLAND_DISPLAY, si no X11. El renderer cairo se mantiene.
#
# Escape (WSLg viejos / si reaparece el bug de popovers en Wayland):
#   EWGUI_FORCE_X11=1   -> vuelve a XWayland (se pierde el resize por borde).
if [ -n "${EWGUI_FORCE_X11:-}" ]; then
    export GDK_BACKEND=x11
else
    # Backend por defecto del sistema (Wayland si esta disponible, si no X11).
    unset GDK_BACKEND
fi
