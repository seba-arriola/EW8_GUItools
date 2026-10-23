/* csnstaevdispQT - arranque del port Qt6 de csnstaevdisp.
 *
 * `ewguiqt::boot()` fija LC_NUMERIC=C antes y despues de QApplication y
 * resuelve los flags headless ANTES de crear la app (funcionan sin DISPLAY).
 *
 * Uso:  csnstaevdispQT <configfile.d>
 *       csnstaevdispQT --print-config <configfile.d>
 *       csnstaevdispQT --check-overlays <configfile.d>
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "ewguiqt/boot.h"

#include "sev_capi.h"
#include "sev_mainwindow.h"

static int run_print_config(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr,
                     "uso: csnstaevdispQT --print-config <configfile.d>\n");
        return 2;
    }
    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    SevConfig *cfg = sev_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnstaevdispQT: %s\n", err);
        return 1;
    }
    sev_print_config(cfg, stdout);
    sev_config_free(cfg);
    return 0;
}

static int run_check_overlays(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr,
                     "uso: csnstaevdispQT --check-overlays <configfile.d>\n");
        return 2;
    }
    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    SevConfig *cfg = sev_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnstaevdispQT: %s\n", err);
        return 1;
    }
    sev_check_overlays(cfg, stdout, stderr);
    sev_config_free(cfg);
    return 0;
}

/* Fixture determinista de los modos instrumentados: 5 estaciones (una a ~1
 * grado al este del centro para probar el anclaje del zoom) y 3 sismos. */
static void fill_fixture(SevSnapshot *s)
{
    static const struct {
        double lat, lon, elev;
    } st[5] = {
        { -30.00, -69.00, -100.0 },
        { -33.45, -70.66,  570.0 },
        { -36.80, -73.00,   20.0 },
        { -23.00, -68.00, 3000.0 },
        { -18.00, -69.50, 5000.0 },
    };
    static const struct {
        double lat, lon, mag;
        int qid;
    } ev[3] = {
        { -31.0, -71.0, 6.2, 101 },
        { -28.0, -70.0, 5.5, 102 },
        { -35.0, -72.0, 4.0, 103 },
    };
    int i;

    std::memset(s, 0, sizeof(*s));
    for (i = 0; i < 5; i++) {
        std::snprintf(s->sta[i].sta, sizeof(s->sta[i].sta), "ST%02d", i + 1);
        s->sta[i].lat = st[i].lat;
        s->sta[i].lon = st[i].lon;
        s->sta[i].elev_m = st[i].elev;
    }
    s->nsta = 5;
    sev_stations_range(s->sta, s->nsta, &s->elev_min, &s->elev_max);

    for (i = 0; i < 3; i++) {
        s->ev[i].lat = ev[i].lat;
        s->ev[i].lon = ev[i].lon;
        s->ev[i].mag = ev[i].mag;
        s->ev[i].qid = ev[i].qid;
    }
    s->nev = 3;
    s->db_ok = 1;
}

static int run_gui(QApplication &app, const QStringList &args)
{
    const char *mode = std::getenv("CSNSTAEVDISPQT_MODE");
    const bool instrumented = (mode && mode[0]);
    char err[512] = {0};

    /* argv[0] + <configfile.d> */
    if (args.size() != 2) {
        std::fprintf(stderr, "uso: csnstaevdispQT <configfile.d>\n");
        return 2;
    }

    const QByteArray cfg_path = args.at(1).toLocal8Bit();
    SevConfig *cfg = sev_config_load(cfg_path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "csnstaevdispQT: %s\n", err);
        return 1;
    }

    SevCore *core = nullptr;
    if (!instrumented) {
        /* Mismo orden que el GTK: BD antes que log/anillo
         * (csnstaevdisp.c:912-934). Un fallo de BD no aborta. */
        core = sev_core_new(cfg);
        if (!core) {
            sev_config_free(cfg);
            return 1;
        }
        (void)sev_log_init(cfg_path.constData(), cfg->log_file);
        if (sev_core_open_db(core) != 0) {
            const char *dberr = sev_core_db_error(core);
            std::fprintf(stderr, "csnstaevdispQT: BD: %s\n",
                         (dberr && dberr[0]) ? dberr : "sin conexion");
        }
        if (sev_core_attach_ring(core) != 0) {
            std::fprintf(stderr,
                         "csnstaevdispQT: no se pudo conectar al anillo <%s>\n",
                         cfg->ring);
            sev_core_free(core);
            sev_config_free(cfg);
            return 2;
        }
        (void)sev_core_refresh(core);
    }

    SevMainWindow win(cfg, core);
    if (instrumented) {
        SevSnapshot fake;
        fill_fixture(&fake);
        win.setSnapshot(fake);
    }
    win.show();

    int rc = 0;
    if (instrumented) {
        /* Modos instrumentados: volcado/geometria/captura deterministas con
         * QT_QPA_PLATFORM=offscreen. No inicializan log, BD ni anillo. */
        QTimer::singleShot(0, &app, [&win, &app, mode, &rc]() {
            if (std::strcmp(mode, "--dump") == 0) {
                std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--layout") == 0) {
                std::fputs(win.dumpLayout().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--interact") == 0) {
                std::fputs(win.interactProbe().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--shot") == 0) {
                const char *p = std::getenv("CSNSTAEVDISPQT_SHOT");
                const QString path = QString::fromUtf8(
                    p && p[0] ? p : "/tmp/csnstaevdispqt_shot.png");
                if (!win.saveShot(path)) {
                    std::fprintf(stderr,
                                 "csnstaevdispQT: no se pudo escribir %s\n",
                                 path.toUtf8().constData());
                    rc = 1;
                } else {
                    std::fprintf(stdout, "shot=%s\n",
                                 path.toUtf8().constData());
                }
            } else {
                std::fprintf(stderr,
                             "csnstaevdispQT: CSNSTAEVDISPQT_MODE desconocido <%s>\n",
                             mode);
                rc = 1;
            }
            app.quit();
        });
    } else {
        const char *ms = std::getenv("CSNSTAEVDISPQT_EXIT_AFTER_MS");
        if (ms && ms[0]) {
            const int t = std::atoi(ms);
            if (t > 0)
                QTimer::singleShot(t, &app, &QCoreApplication::quit);
        }
    }

    const int apprc = app.exec();
    sev_core_free(core);
    sev_config_free(cfg);
    return rc ? rc : apprc;
}

int main(int argc, char **argv)
{
    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("csnstaevdispQT");
    opts.headless.append({ QStringLiteral("--print-config"), run_print_config });
    opts.headless.append({ QStringLiteral("--check-overlays"), run_check_overlays });
    return ewguiqt::boot(argc, argv, opts, run_gui);
}
