/*
 * main.cxx - Punto de entrada de csnhypodbpQT.
 *
 * El orden de resolucion de flags es EXACTAMENTE el de csnhypodbp
 * (csnhypodbp_main.c:2521-2537): los modos headless se atienden ANTES de crear
 * QApplication, asi que funcionan sin DISPLAY, sin PostgreSQL y sin EarthWorm.
 *
 *   csnhypodbpQT --print-config   <archivo.d>   -> stdout, exit 0
 *   csnhypodbpQT --check-profiles <archivo.d>   -> stdout, exit 0/2
 *   csnhypodbpQT <archivo.d> [--state-dir <dir>] -> GUI
 *   otra forma                                  -> uso, exit 1
 *
 * Ganchos de test (equivalen a EWGUI_MODE de los modulos GTK):
 *   BPQ_MODE=--dump | --layout | --shot
 *   BPQ_SHOT=<ruta.png>      (con --shot)
 *   BPQ_EXIT_AFTER_MS=<ms>   (cierra solo; humo)
 *   BPQ_STATE_DIR=<dir>      (equivale a --state-dir)
 *   QT_QPA_PLATFORM=offscreen para correr sin DISPLAY
 */
#include "bpq_capi.h"
#include "bpq_mainwindow.h"

#include <QApplication>
#include <QString>
#include <QTimer>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int print_usage(const char *argv0)
{
    std::fprintf(stderr, "Uso: %s <configfile.d> [--state-dir <dir>]\n", argv0);
    std::fprintf(stderr, "     %s --print-config <configfile.d>\n", argv0);
    std::fprintf(stderr, "     %s --check-profiles <configfile.d>\n", argv0);
    return 1;
}

int main(int argc, char **argv)
{
    const char *cfg_path  = nullptr;
    const char *state_dir = std::getenv("BPQ_STATE_DIR");
    const char *mode      = std::getenv("BPQ_MODE");
    char        err[512]  = "";

    /* El parser de overlays (.lines), perfiles (.tmp) y campos numericos usa
     * strtod/sscanf, que dependen de LC_NUMERIC. Qt llama setlocale(LC_ALL,"")
     * al inicializar su plugin de pantalla, asi que en un escritorio con coma
     * decimal ("es_CL.UTF-8") strtod("-70.43290") corta en "-70" y el resto se
     * rechaza con cientos de "overlay: coordenada invalida". Se fija
     * LC_NUMERIC=C aqui y otra vez despues de crear QApplication (que es quien
     * lo pisa); LC_CTYPE se deja como venga para no romper el texto UTF-8. */
    std::setlocale(LC_ALL, "");
    std::setlocale(LC_NUMERIC, "C");

    /* --- 1/2. modos headless: antes de QApplication (sin DISPLAY) --- */
    if (argc == 3 &&
        (std::strcmp(argv[1], "--print-config") == 0 ||
         std::strcmp(argv[1], "--check-profiles") == 0)) {
        BpqConfig *c = bpq_config_load(argv[2], state_dir, err, sizeof(err));
        int rc;
        if (!c) {
            std::fprintf(stderr, "csnhypodbpQT: %s\n", err);
            return 1;
        }
        rc = (std::strcmp(argv[1], "--print-config") == 0)
                 ? bpq_config_print(c, stdout)
                 : bpq_config_check_profiles(c, stdout);
        bpq_config_free(c);
        return rc;
    }

    /* --- 3. uso --- */
    if (argc >= 2 && argv[1][0] != '-') {
        cfg_path = argv[1];
        if (argc >= 4 && std::strcmp(argv[2], "--state-dir") == 0)
            state_dir = argv[3];
        else if (argc != 2)
            return print_usage(argv[0]);
    } else {
        return print_usage(argv[0]);
    }

    /* --- 4. GUI --- */
    BpqConfig *cfg = bpq_config_load(cfg_path, state_dir, err, sizeof(err));
    if (!cfg) {
        std::fprintf(stderr, "csnhypodbpQT: %s\n", err);
        return 1;
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("csnhypodbpQT"));
    /* El plugin de pantalla de Qt (xcb/wayland) vuelve a llamar setlocale. */
    std::setlocale(LC_NUMERIC, "C");

    BpqMainWindow win(cfg);
    win.show();

    int  rc    = 0;
    bool headless = (mode && mode[0]);
    if (headless) {
        /* Se difiere al bucle de eventos para que la ventana este realizada. */
        QTimer::singleShot(0, &app, [&win, mode, &rc, &app]() {
            /* El fetch de ondas es asincrono: se espera para que el volcado y la
             * captura muestren lo mismo que vera el usuario. */
            win.waitForWaves(3000);
            if (std::strcmp(mode, "--dump") == 0) {
                std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--layout") == 0) {
                std::fputs(win.dumpLayout().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--shot") == 0) {
                /* BPQ_SHOT_LOCATE=1: corre el locate antes de la captura (para
                 * ver el cuadro del preview en estado CANDIDATA).
                 * BPQ_SHOT_MOUSE=1: manda rueda/arrastre/clic reales. */
                if (std::getenv("BPQ_SHOT_LOCATE")) (void)win.runLocateForTest();
                if (std::getenv("BPQ_SHOT_MOUSE")) (void)win.runMouseForTest();
                const char *p = std::getenv("BPQ_SHOT");
                const QString path = QString::fromUtf8(p && p[0] ? p : "/tmp/bpq_shot.png");
                if (!win.saveShot(path)) {
                    std::fprintf(stderr, "csnhypodbpQT: no se pudo escribir %s\n",
                                 path.toUtf8().constData());
                    rc = 1;
                } else {
                    std::fprintf(stdout, "shot=%s\n", path.toUtf8().constData());
                }
            } else if (std::strcmp(mode, "--locate") == 0) {
                /* Cadena real (csnloc + refinador) sobre la solucion elegida. */
                rc = win.runLocateForTest() ? 0 : 1;
            } else if (std::strcmp(mode, "--confirm") == 0) {
                /* locate + Confirm: ARC de confirmacion y publicacion. */
                if (!win.runLocateForTest()) {
                    rc = 1;
                } else {
                    rc = win.runConfirmForTest() ? 0 : 1;
                }
            } else if (std::strcmp(mode, "--mww") == 0) {
                /* Semilla + dry-run del motor (o real con BPQ_MWW_REAL=1). */
                rc = win.runMwwForTest() ? 0 : 1;
            } else if (std::strcmp(mode, "--magcalc") == 0) {
                /* locate + boton `calculate`: csnmags con las ventanas del panel. */
                rc = win.runMagCalcForTest() ? 0 : 1;
            } else if (std::strcmp(mode, "--mouse") == 0) {
                /* Eventos REALES de raton sobre los canvas (zoom/paneo/pick). */
                rc = win.runMouseForTest() ? 0 : 1;
            } else {
                std::fprintf(stderr, "csnhypodbpQT: BPQ_MODE desconocido <%s>\n", mode);
                rc = 1;
            }
            app.quit();
        });
    } else {
        const char *ms = std::getenv("BPQ_EXIT_AFTER_MS");
        if (ms && ms[0]) {
            int t = std::atoi(ms);
            if (t > 0) QTimer::singleShot(t, &app, &QCoreApplication::quit);
        }
    }

    const int apprc = app.exec();
    bpq_config_free(cfg);
    return rc ? rc : apprc;
}
