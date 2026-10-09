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

# GTK4 en WSLg: sobre Wayland (weston 9) los popovers (GtkDropDown, y el
# GtkPopoverMenuBar del menu "Control Panel") se descolocan, desaparecen al clic
# o saltan a otro punto de la pantalla (grab/posicion de xdg_popup roto; ver
# microsoft/wslg#1299, #1226, #1390). Forzamos XWayland (X11), donde funcionan.
# Efecto lateral: la decoracion pasa a ser la CSD de GTK (titlebar con
# minimizar/maximizar/cerrar).
#
# SOLO si hay servidor X: en una maquina Wayland pura (sin XWayland, sin DISPLAY)
# forzar `x11` haria que GTK no pudiera abrir ventana, asi que ahi se deja el
# backend por defecto (Wayland). Se usa `${DISPLAY:-}` porque hay llamadores con
# `set -u` (ew_monitor.sh).
if [ -n "${DISPLAY:-}" ]; then
    export GDK_BACKEND=x11
else
    unset GDK_BACKEND
fi
