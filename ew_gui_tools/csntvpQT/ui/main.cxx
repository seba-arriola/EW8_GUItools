/* csntvpQT - arranque del port Qt6 de csntvp.
 *
 * `ewguiqt::boot()` fija LC_NUMERIC=C antes y despues de QApplication y
 * resuelve los flags headless ANTES de crear la app (funcionan sin DISPLAY).
 *
 * Uso:  csntvpQT <configfile.d>
 *       csntvpQT --print-config <configfile.d>
 *
 * Variables de los modos instrumentados (offscreen, sin anillos ni log):
 *   CSNTVPQT_MODE = --dump | --layout | --shot | --interact
 *   CSNTVPQT_SHOT = ruta del PNG de --shot
 *   CSNTVPQT_EXIT_AFTER_MS = cierra solo tras N ms (test-live)
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

#include "tvq_capi.h"
#include "tvq_mainwindow.h"

static int run_print_config(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr, "uso: csntvpQT --print-config <configfile.d>\n");
        return 2;
    }

    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    TvqConfig *cfg = tvq_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csntvpQT: %s\n", err);
        return 3;
    }
    if (cfg->module_id < 0)
        std::fprintf(stderr,
                     "csntvpQT: aviso: '%s' no resuelve en earthworm.d; "
                     "regenera con worktree_bootstrap.sh . --refresh\n",
                     cfg->my_module);
    tvq_print_config(cfg, stdout);
    tvq_config_free(cfg);
    return 0;
}

static int run_gui(QApplication &app, const QStringList &args)
{
    const char *mode = std::getenv("CSNTVPQT_MODE");
    const bool instrumented = (mode && mode[0]);
    char err[512] = {0};

    /* argv[0] + <configfile.d> */
    if (args.size() != 2) {
        std::fprintf(stderr, "uso: csntvpQT <configfile.d>\n");
        return 2;
    }

    const QByteArray cfg_path = args.at(1).toLocal8Bit();
    TvqConfig *cfg = tvq_config_load(cfg_path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csntvpQT: %s\n", err);
        return 3;
    }
    if (cfg->module_id < 0)
        std::fprintf(stderr,
                     "csntvpQT: aviso: '%s' no resuelve en earthworm.d; "
                     "regenera con worktree_bootstrap.sh . --refresh\n",
                     cfg->my_module);

    if (!instrumented)
        (void)tvq_log_init(cfg->log_file);

    TvqCore *core = tvq_core_new(cfg, err, sizeof err);
    if (!core) {
        std::fprintf(stderr, "csntvpQT: %s\n", err);
        tvq_config_free(cfg);
        return 3;
    }

    if (tvq_core_load_stations(core, err, sizeof err) != 0) {
        std::fprintf(stderr, "csntvpQT: %s\n", err);
        tvq_core_free(core);
        tvq_config_free(cfg);
        return 3;
    }

    if (!instrumented) {
        if (tvq_core_open_rings(core, err, sizeof err) != 0) {
            std::fprintf(stderr, "csntvpQT: %s\n", err);
            tvq_core_free(core);
            tvq_config_free(cfg);
            return 4;
        }
    } else {
        /* Fixture determinista: 3 estaciones del .sta de prueba con 40 s de
         * ondas y dos picadas automaticas. */
        (void)tvq_core_seed_fixture(core, tvq_now_epoch() - 2.0, 40);
    }

    TvqMainWindow win(cfg, core);
    win.setInstrumented(instrumented);
    win.show();

    int rc = 0;
    if (instrumented) {
        QTimer::singleShot(0, &app, [&win, &app, mode, &rc]() {
            if (std::strcmp(mode, "--dump") == 0) {
                std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--layout") == 0) {
                std::fputs(win.dumpLayout().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--interact") == 0) {
                std::fputs(win.interactProbe().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--shot") == 0) {
                const char *p = std::getenv("CSNTVPQT_SHOT");
                const QString path =
                    QString::fromUtf8(p && p[0] ? p : "/tmp/csntvpqt_shot.png");
                if (!win.saveShot(path)) {
                    std::fprintf(stderr, "csntvpQT: no pude escribir %s\n",
                                 path.toUtf8().constData());
                    rc = 1;
                } else {
                    std::fprintf(stdout, "shot=%s\n",
                                 path.toUtf8().constData());
                }
            } else {
                std::fprintf(stderr, "csntvpQT: CSNTVPQT_MODE desconocido <%s>\n",
                             mode);
                rc = 1;
            }
            app.quit();
        });
    } else {
        const char *ms = std::getenv("CSNTVPQT_EXIT_AFTER_MS");
        if (ms && ms[0]) {
            const int t = std::atoi(ms);
            if (t > 0)
                QTimer::singleShot(t, &app, &QCoreApplication::quit);
        }
    }

    const int apprc = app.exec();
    tvq_core_free(core);
    tvq_config_free(cfg);
    return rc ? rc : apprc;
}

int main(int argc, char **argv)
{
    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("csntvpQT");
    opts.headless.append({ QStringLiteral("--print-config"), run_print_config });
    return ewguiqt::boot(argc, argv, opts, run_gui);
}
