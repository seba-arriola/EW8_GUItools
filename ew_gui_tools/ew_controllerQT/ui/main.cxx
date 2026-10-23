/* ew_controllerQT - arranque del port Qt6 de ew_controller.
 *
 * `ewguiqt::boot()` hace lo que todos los main de los modulos Qt repiten:
 * LC_NUMERIC=C antes y despues de QApplication, y los flags headless resueltos
 * antes de crear la app (funcionan sin DISPLAY).
 *
 * Uso:  ew_controllerQT <configfile.d>
 *       ew_controllerQT --print-config <configfile.d>
 */
#include <cstdio>

#include <QApplication>
#include <QStringList>
#include <QTimer>

#include "ewguiqt/boot.h"

#include "ecq_capi.h"
#include "ecq_mainwindow.h"

static int run_print_config(const QStringList &args)
{
    if (args.size() != 1) {
        std::fprintf(stderr, "uso: ew_controllerQT --print-config <configfile.d>\n");
        return 2;
    }
    char err[512] = {0};
    const QByteArray path = args.at(0).toLocal8Bit();
    EcqConfig *cfg = ecq_config_load(path.constData(), err, sizeof err);
    if (!cfg) {
        std::fprintf(stderr, "ew_controllerQT: %s\n", err);
        return 1;
    }
    ecq_print_config(cfg, stdout);
    ecq_config_free(cfg);
    return 0;
}

static int run_gui(QApplication &app, const QStringList &args)
{
    const char *mode = std::getenv("ECQ_MODE");
    char       err[512] = "";

    /* argv[0] + <configfile.d> */
    if (args.size() != 2) {
        std::fprintf(stderr, "uso: ew_controllerQT <configfile.d>\n");
        return 2;
    }

    const QByteArray cfg_path = args.at(1).toLocal8Bit();
    EcqConfig *cfg = ecq_config_load(cfg_path.constData(), err, sizeof(err));
    if (!cfg) {
        std::fprintf(stderr, "ew_controllerQT: %s\n", err);
        return 1;
    }

    /* Log propio en $EW_LOG (`LogFile` del .d). Solo en el arranque normal: los
     * modos instrumentados (ECQ_MODE) son el oraculo del arnes y no deben
     * escribir ficheros. */
    if (!mode || !mode[0])
        (void)ecq_log_init(cfg->log_file);

    EcqMainWindow win(cfg);
    win.show();

    int rc = 0;
    if (mode && mode[0]) {
        /* Modos instrumentados: volcado/geometria/captura deterministas, con
         * QT_QPA_PLATFORM=offscreen. Es el oraculo de paridad del arnés. */
        QTimer::singleShot(0, &app, [&win, &app, mode, &rc]() {
            win.waitForSeq(1, 2000);
            const char *sel = std::getenv("ECQ_SELECT_PID");
            if (sel && sel[0])
                win.selectPidForTest(std::atoi(sel));

            if (std::strcmp(mode, "--dump") == 0) {
                std::fputs(win.dumpTree().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--layout") == 0) {
                std::fputs(win.dumpLayout().toUtf8().constData(), stdout);
            } else if (std::strcmp(mode, "--shot") == 0) {
                const char *p = std::getenv("ECQ_SHOT");
                const QString path = QString::fromUtf8(p && p[0] ? p : "/tmp/ecq_shot.png");
                if (!win.saveShot(path)) {
                    std::fprintf(stderr, "ew_controllerQT: no se pudo escribir %s\n",
                                 path.toUtf8().constData());
                    rc = 1;
                } else {
                    std::fprintf(stdout, "shot=%s\n", path.toUtf8().constData());
                }
            } else {
                std::fprintf(stderr, "ew_controllerQT: ECQ_MODE desconocido <%s>\n", mode);
                rc = 1;
            }
            app.quit();
        });
    } else {
        const char *ms = std::getenv("ECQ_EXIT_AFTER_MS");
        if (ms && ms[0]) {
            const int t = std::atoi(ms);
            if (t > 0)
                QTimer::singleShot(t, &app, &QCoreApplication::quit);
        }
    }

    const int apprc = app.exec();
    ecq_config_free(cfg);
    return rc ? rc : apprc;
}

int main(int argc, char **argv)
{
    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("ew_controllerQT");
    opts.headless.append({QStringLiteral("--print-config"), run_print_config});
    return ewguiqt::boot(argc, argv, opts, run_gui);
}
