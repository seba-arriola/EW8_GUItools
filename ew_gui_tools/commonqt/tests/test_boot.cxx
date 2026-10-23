/* C4: `boot()` resuelve los flags headless SIN crear QApplication (funcionan
 * sin DISPLAY) y deja LC_NUMERIC en "C" tambien despues de crear la app. */
#include <clocale>
#include <cstdio>
#include <cstring>

#include <QApplication>

#include "ewguiqt/boot.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);          \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

static const char *numericLocale()
{
    const char *s = setlocale(LC_NUMERIC, nullptr);
    return s ? s : "";
}

static bool forceCommaLocale()
{
    /* Si la maquina tiene un locale con coma decimal, arrancamos con el puesto
     * para que el test compruebe de verdad que boot() lo pisa. */
    static const char *candidates[] = {"de_DE.UTF-8", "es_CL.UTF-8", "fr_FR.UTF-8",
                                       "es_ES.UTF-8", "pt_BR.UTF-8"};
    for (const char *c : candidates) {
        if (setlocale(LC_NUMERIC, c)) {
            std::printf("nota: arrancando con LC_NUMERIC=%s\n", c);
            return true;
        }
    }
    std::printf("nota: sin locale con coma decimal; se comprueba solo que quede en C\n");
    return false;
}

int main()
{
    forceCommaLocale();

    int headless_rc = -1;
    bool saw_app = true;

    ewguiqt::BootOptions opts;
    opts.app_name = QStringLiteral("probeqt");
    opts.headless.append({QStringLiteral("--probe"), [&](const QStringList &args) {
        saw_app = QApplication::instance() != nullptr;
        headless_rc = args.size() == 2 && args.at(0) == "uno" && args.at(1) == "dos" ? 7 : 9;
        (void)numericLocale();
        return headless_rc;
    }});

    char arg0[] = "probe";
    char arg1[] = "--probe";
    char arg2[] = "uno";
    char arg3[] = "dos";
    char *argv_headless[] = {arg0, arg1, arg2, arg3, nullptr};

    const int rc = ewguiqt::boot(4, argv_headless, opts,
        [](QApplication &, const QStringList &) {
            return 99;   /* no debe llamarse: el flag headless gana */
        });
    CHECK(rc == 7, "--probe no devolvio su exit code");
    CHECK(!saw_app, "se creo QApplication antes de resolver el flag headless");

    /* Caso GUI: solo se puede crear una QApplication por proceso, asi que este
     * es el ultimo caso del binario. */
    bool gui_called = false;
    char *argv_gui[] = {arg0, nullptr};
    const int rc_gui = ewguiqt::boot(1, argv_gui, opts,
        [&](QApplication &app, const QStringList &args) {
            gui_called = true;
            CHECK(args.size() == 1, "argv no llega completo al callback");
            CHECK(QString::fromLocal8Bit(numericLocale()) == QStringLiteral("C"),
                  "LC_NUMERIC no quedo en C tras crear QApplication");
            CHECK(app.applicationName() == QStringLiteral("probeqt"),
                  "applicationName no se aplico");
            return 0;
        });
    CHECK(rc_gui == 0, "el callback GUI devolvio un rc distinto");
    CHECK(gui_called, "no se llamo al callback GUI");

    if (g_fail == 0)
        std::printf("OK: boot() fija el locale y resuelve headless sin DISPLAY\n");
    return g_fail;
}
