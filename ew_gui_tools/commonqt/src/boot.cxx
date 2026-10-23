/* Ver include/ewguiqt/boot.h. */
#include "ewguiqt/boot.h"

#include <clocale>

#include <QApplication>
#include <QByteArray>

namespace ewguiqt {

int boot(int argc, char **argv, const BootOptions &opts,
         const std::function<int(QApplication &, const QStringList &)> &gui)
{
    /* Antes de Qt: el locale del sistema para los mensajes, el numerico a C
     * para que strtod/sscanf de la capa C no lean "3,38". */
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    if (!opts.app_name.isEmpty())
        QApplication::setApplicationName(opts.app_name);

    QStringList args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    /* Flags headless ANTES de QApplication: no requieren DISPLAY ni plugin de
     * plataforma (asi `--print-config` corre en un servidor). */
    for (const HeadlessCommand &cmd : opts.headless) {
        if (args.size() >= 2 && args.at(1) == cmd.flag)
            return cmd.run(args.mid(2));
    }

    QApplication app(argc, argv);
    /* Qt puede re-aplicar el locale del sistema al construir la app. */
    setlocale(LC_NUMERIC, "C");
    if (!opts.app_name.isEmpty())
        app.setApplicationDisplayName(opts.app_name);

    return gui(app, args);
}

} /* namespace ewguiqt */
