/*
 * main.cxx - Punto de entrada de csnhypodbpQT.
 *
 * El orden de resolucion de flags es EXACTAMENTE el de csnhypodbp
 * (csnhypodbp_main.c:2521-2537): los modos headless se atienden ANTES de crear
 * QApplication, asi que funcionan sin DISPLAY, sin PostgreSQL y sin EarthWorm.
 * Eso (y el LC_NUMERIC=C antes/despues de Qt) lo hace ewguiqt::boot().
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
#include <QImageReader>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ewguiqt/boot.h"

static int print_usage(const QString &argv0)
{
    const QByteArray p = argv0.toLocal8Bit();
    std::fprintf(stderr, "Uso: %s <configfile.d> [--state-dir <dir>]\n", p.constData());
    std::fprintf(stderr, "     %s --print-config <configfile.d>\n", p.constData());
    std::fprintf(stderr, "     %s --check-profiles <configfile.d>\n", p.constData());
    return 1;
}

/* --print-config / --check-profiles: sin QApplication (sin DISPLAY). */
static int run_config_cmd(const char *flag, const QStringList &args)
{
    char err[512] = "";
    const char *state_dir = std::getenv("BPQ_STATE_DIR");

    if (args.size() != 1)
        return print_usage(QStringLiteral("csnhypodbpQT"));

    const QByteArray path = args.at(0).toLocal8Bit();
    BpqConfig *c = bpq_config_load(path.constData(), state_dir, err, sizeof(err));
    if (!c) {
        std::fprintf(stderr, "csnhypodbpQT: %s\n", err);
        return 1;
    }
    const int rc = (std::strcmp(flag, "--print-config") == 0)
                       ? bpq_config_print(c, stdout)
                       : bpq_config_check_profiles(c, stdout);
    bpq_config_free(c);
    return rc;
}

static int run_gui(QApplication &app, const QStringList &args)
{
    const char *state_dir = std::getenv("BPQ_STATE_DIR");
    const char *mode      = std::getenv("BPQ_MODE");
    QByteArray state_dir_buf;
    char       err[512] = "";

    /* argv[0] + <configfile.d> [--state-dir <dir>] */
    if (args.size() < 2 || args.at(1).startsWith(QLatin1Char('-')))
        return print_usage(args.value(0));

    const QByteArray cfg_path = args.at(1).toLocal8Bit();
    if (args.size() == 4 && args.at(2) == QStringLiteral("--state-dir")) {
        state_dir_buf = args.at(3).toLocal8Bit();
        state_dir = state_dir_buf.constData();
    } else if (args.size() != 2) {
        return print_usage(args.value(0));
    }

    /* Qt6 (hasta 6.4) limita por defecto la decodificacion a 128 MB y el mapa
     * 2:1 de 10800x5400 ocupa 233 MB en ARGB32: sin esto QImage RECHAZA la
     * imagen ("Rejecting image as it exceeds the current allocation limit of
     * 128 megabytes") y el panel de planta se queda sin fondo, en silencio.
     * El limite es global del proceso (vale tambien para los mapas futuros) y
     * `QT_IMAGEIO_MAXALLOC` lo sobreescribe en runtime. Mismo valor que csnrvQT. */
    QImageReader::setAllocationLimit(1024);

    BpqConfig *cfg = bpq_config_load(cfg_path.constData(), state_dir, err, sizeof(err));
    if (!cfg) {
        std::fprintf(stderr, "csnhypodbpQT: %s\n", err);
        return 1;
    }
    /* P teorica: sin el modelo cargado no hay ancla para las estaciones sin
     * picada M/A y la tabla de magnitudes se queda sin ventanas. Se avisa y
     * se sigue (la GUI no muere por un recurso opcional). */
    {
        char terr[256] = "";

        if (!bpq_init_tt(cfg, terr, sizeof(terr)) && terr[0])
            std::fprintf(stderr, "csnhypodbpQT: %s\n", terr);
    }
    /* P teorica en el dominio regional con el MISMO modelo de capas con el que
     * hyp2000 localiza (bandas Potin del .hyp del localizador). Sin el, la P
     * teorica se queda en IASP91 como antes. */
    {
        char terr[256] = "";

        if (!bpq_tt_theo_init(cfg, terr, sizeof(terr)) && terr[0])
            std::fprintf(stderr, "csnhypodbpQT: %s\n", terr);
    }

    BpqMainWindow win(cfg);
    /* Tamano de ventana para los tests headless (--dump/--layout/--shot):
     * BPQ_WIN_SIZE=1600x900. Sin la variable queda el 1100x750 de siempre. */
    if (const char *ws = std::getenv("BPQ_WIN_SIZE")) {
        int w = 0, h = 0;

        if (std::sscanf(ws, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
            win.resize(w, h);
    }
    win.show();

    int rc = 0;
    const bool headless = (mode && mode[0]);
    if (headless) {
        /* Se difiere al bucle de eventos para que la ventana este realizada. */
        QTimer::singleShot(0, &app, [&win, mode, &rc, &app]() {
            /* Fixture/pin/click de la fase 7 (BPQ_TEST_*). */
            win.runTestHooks();
            /* El fetch de ondas es asincrono: se espera para que el volcado y la
             * captura muestren lo mismo que vera el usuario. */
            const char *wms = std::getenv("BPQ_WAVE_WAIT_MS");
            const int wait_ms = (wms && wms[0]) ? std::atoi(wms) : 3000;
            win.waitForWaves(wait_ms > 0 ? wait_ms : 0);
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
            } else if (std::strcmp(mode, "--magdrag") == 0) {
                /* Arrastre de la franja de magnitudes: dueno y cancelacion. */
                rc = win.runMagDragForTest() ? 0 : 1;
            } else if (std::strcmp(mode, "--input") == 0) {
                /* Una tecla/rueda a un widget concreto + volcado del estado. */
                if (!win.runInputForTest())
                    rc = 1;
                else
                    std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--sort") == 0) {
                /* Clic REAL en una cabecera + volcado del orden resultante. */
                if (!win.runSortForTest())
                    rc = 1;
                else
                    std::fputs(win.dumpTree().toUtf8().constData(), stdout);
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

int main(int argc, char **argv)
{
    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("csnhypodbpQT");
    opts.headless.append({QStringLiteral("--print-config"),
                          [](const QStringList &a) { return run_config_cmd("--print-config", a); }});
    opts.headless.append({QStringLiteral("--check-profiles"),
                          [](const QStringList &a) { return run_config_cmd("--check-profiles", a); }});
    return ewguiqt::boot(argc, argv, opts, run_gui);
}
