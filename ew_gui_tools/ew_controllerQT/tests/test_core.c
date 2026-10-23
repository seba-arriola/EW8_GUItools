/* C8/C9/C10/C11: vista, sensibilidad, "sin respuesta" y protocolo de acciones
 * del nucleo de ew_controllerQT, sobre los MISMOS fixtures que usa el arnés del
 * modulo GTK (`ew_controller/tests/fixtures/status_*.txt`).
 *
 * C puro: no necesita Qt, ni DISPLAY, ni anillo (la ruta del anillo se prueba en
 * test-live con TEST_CTRL_RING). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ecq_capi.h"

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "."
#endif

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);              \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

static EcqConfig make_cfg(void)
{
    EcqConfig c;

    memset(&c, 0, sizeof(c));
    snprintf(c.my_module, sizeof(c.my_module), "MOD_EW_CONTROLLERQT");
    snprintf(c.ring, sizeof(c.ring), "TEST_CTRL_RING");
    c.heart_beat_int = 30;
    c.poll_int = 5;
    c.log_file = 1;
    snprintf(c.log_dir, sizeof(c.log_dir), "/tmp");
    return c;
}

/* Devuelve 1 si aplico el fixture. */
static int apply_fixture(EcqCore *core, const char *name)
{
    char path[512];
    char buf[16384];
    FILE *f;
    size_t n;

    snprintf(path, sizeof(path), "%s/%s", FIXTURES_DIR, name);
    f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: no se pudo abrir el fixture %s\n", path);
        g_fail = 1;
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return ecq_apply_status(core, buf) == 0;
}

static void check_status_alive(void)
{
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);
    const EcqTable *t;

    CHECK(c != NULL, "no se pudo crear el core");
    if (!c)
        return;
    CHECK(apply_fixture(c, "status_alive.txt"), "no se aplico status_alive");

    t = ecq_table(c);
    CHECK(t->nmods == 2, "status_alive deberia dar 2 modulos");
    CHECK(t->nrings == 2, "status_alive deberia dar 2 anillos");
    CHECK(t->seen_status == 1, "seen_status deberia quedar a 1");
    CHECK(strcmp(t->hostname, "probe (ew_controller gui test)") == 0, "hostname mal");
    CHECK(strcmp(t->start, "2026-01-01 00:00:00") == 0, "start time mal");
    CHECK(strcmp(t->current, "2026-01-01 00:05:00") == 0, "current time mal");
    CHECK(strcmp(t->disk, "1024 MB") == 0, "disk mal");
    CHECK(strcmp(t->version, "probe") == 0, "version mal");

    CHECK(strcmp(t->mods[0].name, "ZZ_probe") == 0 && t->mods[0].pid == 33,
          "primera fila mal");
    CHECK(strcmp(t->mods[0].status, "Alive") == 0, "status de ZZ_probe mal");
    CHECK(strcmp(t->mods[0].cfgfile, "probe_z.d") == 0, "cfgfile de ZZ_probe mal");
    CHECK(strcmp(t->mods[1].name, "AA_probe") == 0 && t->mods[1].pid == 200,
          "segunda fila mal");
    CHECK(strcmp(t->mods[1].details, "?\?/ 0 00:00:50  probe_a.d") == 0,
          "Details deberia ser la cola cruda de la linea");

    CHECK(strcmp(t->rings[0].name, "WILDCARD_RING") == 0 && t->rings[0].key == 1000 &&
          t->rings[0].size == 16, "primer anillo mal");
    CHECK(strcmp(t->rings[1].name, "ZZZ_RING") == 0 && t->rings[1].key == 20 &&
          t->rings[1].size == 512, "segundo anillo mal");

    ecq_core_free(c);
}

static void check_identity_and_gating(void)
{
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);

    if (!c) {
        g_fail = 1;
        return;
    }

    CHECK(apply_fixture(c, "status_alive.txt"), "no se aplico status_alive");
    CHECK(ecq_selected_pid(c) == 0, "no deberia haber seleccion inicial");
    CHECK(ecq_select_pid(c, 200) == 0, "no se pudo seleccionar el pid 200");
    CHECK(strcmp(ecq_target_label(c), "AA_probe (Alive)") == 0, "target mal");

    CHECK(ecq_can(c, ECQ_ACT_STOP, 200) == 1, "Stop deberia estar activo con Alive");
    CHECK(ecq_can(c, ECQ_ACT_RESTART, 200) == 1, "Restart deberia estar activo");
    CHECK(ecq_can(c, ECQ_ACT_START, 200) == 0, "Start no deberia estar activo con Alive");
    CHECK(ecq_can(c, ECQ_ACT_RECONFIG, 0) == 1, "Reconfig deberia estar activo tras un status");

    /* Dead: Start si, Stop no. */
    CHECK(apply_fixture(c, "status_dead.txt"), "no se aplico status_dead");
    CHECK(ecq_selected_pid(c) == 200, "la seleccion por pid debe sobrevivir al rebuild");
    CHECK(ecq_can(c, ECQ_ACT_STOP, 200) == 0, "Stop no deberia estar activo con Dead");
    CHECK(ecq_can(c, ECQ_ACT_START, 200) == 1, "Start deberia estar activo con Dead");
    CHECK(ecq_can(c, ECQ_ACT_RESTART, 200) == 1, "Restart deberia estar activo con Dead");

    /* Un pid que ya no esta: la seleccion se limpia y nada queda activo. */
    CHECK(ecq_select_pid(c, 201) == -1, "seleccionar un pid ausente deberia fallar");
    CHECK(ecq_selected_pid(c) == 0, "la seleccion deberia quedar limpia");
    CHECK(ecq_can(c, ECQ_ACT_RESTART, 201) == 0, "Restart no deberia estar activo sin fila");

    ecq_core_free(c);
}

static void check_dup_names(void)
{
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);
    const EcqTable *t;

    if (!c) {
        g_fail = 1;
        return;
    }
    CHECK(apply_fixture(c, "status_dup_a.txt"), "no se aplico status_dup_a");
    t = ecq_table(c);
    CHECK(t->nmods == 2, "status_dup_a deberia dar 2 filas con el mismo nombre");
    if (t->nmods == 2) {
        CHECK(strcmp(t->mods[0].name, "AA_probe") == 0 && t->mods[0].pid == 200,
              "fila 200 mal");
        CHECK(strcmp(t->mods[1].name, "AA_probe") == 0 && t->mods[1].pid == 201,
              "fila 201 mal");
        CHECK(strcmp(t->mods[0].cfgfile, "probe_a.d") == 0, "cfgfile de 200 mal");
        CHECK(strcmp(t->mods[1].cfgfile, "probe_b.d") == 0, "cfgfile de 201 mal");
    }

    /* Identidad por pid: seleccionar 201 y que sobreviva a un rebuild. */
    CHECK(ecq_select_pid(c, 201) == 0, "no se pudo seleccionar 201");
    CHECK(apply_fixture(c, "status_dup_b.txt"), "no se aplico status_dup_b");
    CHECK(ecq_selected_pid(c) == 201, "la seleccion por pid no sobrevivio");
    CHECK(strcmp(ecq_target_label(c), "AA_probe (Stop)") == 0, "target de 201 mal");
    CHECK(ecq_can(c, ECQ_ACT_START, 201) == 1, "Start deberia estar activo para 201 (Stop)");

    ecq_core_free(c);
}

static void check_startstop_is_untouchable(void)
{
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);
    /* Status sintetico: startstop Alive en medio. */
    const char *msg =
        "Hostname-OS: probe\n"
        "Start time (UTC): 2026-01-01 00:00:00\n"
        "Current time (UTC): 2026-01-01 00:05:00\n"
        "Disk space avail: 1 MB\n"
        "Stopstart Version: probe\n"
        "-------     -------      ---  ------      --------\n"
        "startstop             100  Alive       ?\?/ 0 00:00:50  startstop_unix.d\n"
        "AA_probe              200  Alive       ?\?/ 0 00:00:50  probe_a.d\n";

    if (!c) {
        g_fail = 1;
        return;
    }
    CHECK(ecq_apply_status(c, msg) == 0, "no se aplico el status sintetico");
    CHECK(ecq_can(c, ECQ_ACT_STOP, 100) == 0, "Stop sobre startstop debe estar prohibido");
    CHECK(ecq_can(c, ECQ_ACT_RESTART, 100) == 0, "Restart sobre startstop debe estar prohibido");
    CHECK(ecq_can(c, ECQ_ACT_START, 100) == 0, "Start sobre startstop debe estar prohibido");
    CHECK(ecq_can(c, ECQ_ACT_STOP, 200) == 1, "Stop sobre un modulo normal deberia estar activo");
    CHECK(ecq_can(c, ECQ_ACT_RECONFIG, 0) == 1, "Reconfig no depende de la seleccion");
    ecq_core_free(c);
}

static void check_noreply(void)
{
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);
    const EcqTable *t;
    const time_t now = time(NULL);

    if (!c) {
        g_fail = 1;
        return;
    }
    CHECK(apply_fixture(c, "status_alive.txt"), "no se aplico status_alive");
    CHECK(ecq_select_pid(c, 200) == 0, "no se pudo seleccionar 200");

    /* PollInt=5 => el umbral es 20 s. */
    CHECK(ecq_noreply(c, now + 20) == 0, "con 20 s no deberia marcar sin respuesta");
    CHECK(ecq_noreply(c, now + 21) == 1, "con 21 s deberia marcar sin respuesta");

    t = ecq_table(c);
    CHECK(strcmp(t->mods[1].status, "?") == 0, "sin respuesta la columna Status deberia ser '?'");
    CHECK(strcmp(ecq_target_label(c), "AA_probe (?)") == 0, "el target deberia decir '?'");
    CHECK(ecq_can(c, ECQ_ACT_STOP, 200) == 0, "sin respuesta Stop debe apagarse");
    CHECK(ecq_can(c, ECQ_ACT_RESTART, 200) == 0, "sin respuesta Restart debe apagarse");
    CHECK(ecq_can(c, ECQ_ACT_START, 200) == 0, "sin respuesta Start debe apagarse");
    CHECK(ecq_can(c, ECQ_ACT_RECONFIG, 0) == 0, "sin respuesta Reconfig debe apagarse");

    /* Vuelve la respuesta: se restaura el status real. */
    CHECK(apply_fixture(c, "status_alive.txt"), "no se reaplico status_alive");
    t = ecq_table(c);
    CHECK(strcmp(t->mods[1].status, "Alive") == 0, "con respuesta el Status debe volver");
    CHECK(ecq_noreply(c, time(NULL)) == 0, "con respuesta reciente no debe marcar noreply");

    ecq_core_free(c);
}

static void check_action_messages(void)
{
    char buf[32];

    CHECK(strcmp(ecq_action_message(ECQ_ACT_STOP, 201, buf, sizeof buf), "TYPE_STOP") == 0,
          "Stop debe emitir TYPE_STOP");
    CHECK(strcmp(buf, "201") == 0, "Stop debe llevar el pid 201, no otro");

    CHECK(strcmp(ecq_action_message(ECQ_ACT_RESTART, 201, buf, sizeof buf), "TYPE_RESTART") == 0,
          "Restart debe emitir TYPE_RESTART");
    CHECK(strcmp(buf, "201") == 0, "Restart debe llevar el pid");

    /* El GTK no tiene TYPE_START: Start se manda como TYPE_RESTART. */
    CHECK(strcmp(ecq_action_message(ECQ_ACT_START, 200, buf, sizeof buf), "TYPE_RESTART") == 0,
          "Start debe emitir TYPE_RESTART (no existe TYPE_START)");
    CHECK(strcmp(buf, "200") == 0, "Start debe llevar el pid");

    CHECK(strcmp(ecq_action_message(ECQ_ACT_RECONFIG, 0, buf, sizeof buf), "TYPE_RECONFIG") == 0,
          "Reconfig debe emitir TYPE_RECONFIG");
    CHECK(buf[0] == '\0', "Reconfig no lleva payload");

    /* Sin anillo, la accion falla sin tocar nada. */
    const EcqConfig cfg = make_cfg();
    EcqCore *c = ecq_core_new(&cfg);
    char detail[64] = "sin tocar";
    CHECK(ecq_action(c, ECQ_ACT_STOP, 200, detail, sizeof detail) == -1,
          "sin anillo adjunto la accion deberia fallar");
    CHECK(detail[0] == '\0', "un fallo no debe dejar un 'sent' en la statusbar");
    ecq_core_free(c);
}

int main(void)
{
    check_status_alive();
    check_identity_and_gating();
    check_dup_names();
    check_startstop_is_untouchable();
    check_noreply();
    check_action_messages();

    if (g_fail == 0)
        printf("OK: estado, sensibilidad, noreply y acciones por pid\n");
    return g_fail;
}
