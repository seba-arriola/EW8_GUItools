/* csnrvQT - arranque del port Qt6 de csnrv.
 *
 * `ewguiqt::boot()` fija LC_NUMERIC=C antes y despues de QApplication y
 * resuelve los flags headless ANTES de crear la app (funcionan sin DISPLAY).
 *
 * Uso:  csnrvQT <configfile.d>
 *       csnrvQT --print-config <configfile.d>
 *       csnrvQT --check-overlays <configfile.d>
 *       csnrvQT --test-mag
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "ewguiqt/boot.h"

#include "crv_capi.h"
#include "crv_mainwindow.h"

static int run_print_config(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr, "uso: csnrvQT --print-config <configfile.d>\n");
        return 2;
    }
    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    CrvConfig *cfg = crv_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnrvQT: %s\n", err);
        return 1;
    }
    crv_print_config(cfg, stdout);
    crv_config_free(cfg);
    return 0;
}

static int run_check_overlays(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr, "uso: csnrvQT --check-overlays <configfile.d>\n");
        return 2;
    }
    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    CrvConfig *cfg = crv_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnrvQT: %s\n", err);
        return 1;
    }
    crv_check_overlays(cfg, stdout, stderr);
    crv_config_free(cfg);
    return 0;
}

static int run_test_mag(const QStringList &args)
{
    if (!args.isEmpty()) {
        std::fprintf(stderr, "uso: csnrvQT --test-mag\n");
        return 2;
    }
    return crv_selftest_mag(stdout) == 0 ? 0 : 1;
}

static void fill_fixture(CrvSnapshot *s)
{
    std::memset(s, 0, sizeof(*s));
    s->has_data = 1;
    s->has_depth = 1;
    s->origin_time = 1790000000.0;
    s->lat = -20.5;
    s->lon = -70.25;
    s->depth_km = 12.0;
    s->qid = 42;
    s->version = 3;
    crv_mag_reset(&s->mag, 42);
    s->mag.ml = 4.7;
    s->mag.n_ml = 5;
    s->mag.mwp = 6.0;
    s->mag.n_mwp = 4;
    s->mag.mb = 5.2;
    s->mag.n_mb = 6;
    s->mag.ms = 5.6;
    s->mag.n_ms = 5;
}

static int run_gui(QApplication &app, const QStringList &args)
{
    const char *mode = std::getenv("CSNRVQT_MODE");
    const bool instrumented = (mode && mode[0]);
    char err[512] = {0};

    /* argv[0] + <configfile.d> */
    if (args.size() != 2) {
        std::fprintf(stderr, "uso: csnrvQT <configfile.d>\n");
        return 2;
    }

    const QByteArray cfg_path = args.at(1).toLocal8Bit();
    CrvConfig *cfg = crv_config_load(cfg_path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnrvQT: %s\n", err);
        return 1;
    }

    CrvCore *core = nullptr;
    if (!instrumented) {
        (void)crv_log_init(cfg->log_file);

        core = crv_core_new(cfg);
        if (!core) {
            crv_config_free(cfg);
            return 1;
        }
        (void)crv_core_open_db(core);
        if (crv_core_attach_ring(core) != 0) {
            std::fprintf(stderr,
                         "csnrvQT: no se pudo conectar al anillo <%s>\n",
                         cfg->ring);
            crv_core_free(core);
            crv_config_free(cfg);
            return 2;
        }
        (void)crv_core_seed_db(core);
    }

    CrvMainWindow win(cfg, core);
    if (instrumented) {
        CrvSnapshot fake;
        fill_fixture(&fake);
        win.setSnapshot(fake);
    }
    win.show();

    int rc = 0;
    if (instrumented) {
        /* Modos instrumentados: volcado/geometria/captura deterministas con
         * QT_QPA_PLATFORM=offscreen. No inicializan log ni abren anillo. */
        QTimer::singleShot(0, &app, [&win, &app, mode, &rc]() {
            if (std::strcmp(mode, "--dump") == 0) {
                std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--layout") == 0) {
                std::fputs(win.dumpLayout().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--interact") == 0) {
                std::fputs(win.interactProbe().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--shot") == 0) {
                const char *p = std::getenv("CSNRVQT_SHOT");
                const QString path =
                    QString::fromUtf8(p && p[0] ? p : "/tmp/csnrvqt_shot.png");
                if (!win.saveShot(path)) {
                    std::fprintf(stderr, "csnrvQT: no se pudo escribir %s\n",
                                 path.toUtf8().constData());
                    rc = 1;
                } else {
                    std::fprintf(stdout, "shot=%s\n",
                                 path.toUtf8().constData());
                }
            } else {
                std::fprintf(stderr, "csnrvQT: CSNRVQT_MODE desconocido <%s>\n",
                             mode);
                rc = 1;
            }
            app.quit();
        });
    } else {
        const char *ms = std::getenv("CSNRVQT_EXIT_AFTER_MS");
        if (ms && ms[0]) {
            const int t = std::atoi(ms);
            if (t > 0)
                QTimer::singleShot(t, &app, &QCoreApplication::quit);
        }
    }

    const int apprc = app.exec();
    crv_core_free(core);
    crv_config_free(cfg);
    return rc ? rc : apprc;
}

int main(int argc, char **argv)
{
    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("csnrvQT");
    opts.headless.append({ QStringLiteral("--print-config"), run_print_config });
    opts.headless.append({ QStringLiteral("--check-overlays"), run_check_overlays });
    opts.headless.append({ QStringLiteral("--test-mag"), run_test_mag });
    return ewguiqt::boot(argc, argv, opts, run_gui);
}
