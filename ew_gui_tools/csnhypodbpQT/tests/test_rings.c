/*
 * test_rings.c - Contratos de la capa de anillos que se pueden verificar sin
 * anillos vivos (AC9 y errores de apertura):
 *
 *   1. bpq_pick_msg_format  -> bytes EXACTOS del TYPE_PICK_SCNL (incluye el caso
 *      de segundos < 10 que con "%06.3f" metia un espacio y csnloc descartaba).
 *   2. bpq_mag_msg_format   -> wr_mag_scnl + bpq_magmsg_parse (ida y vuelta).
 *   3. bpq_magmsg_parse     -> no toca el buffer del llamante (rd_mag parsea
 *      in-place) y no desborda con un mensaje con mas entradas que '\n'.
 *   4. bpq_rings_open       -> falla ANTES de adjuntar si el anillo o el modulo
 *      no existen, y el error nombra el culpable.
 *
 * Se ejecuta con cwd = run_working_v8/params (GetKey/GetModId leen earthworm.d).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rw_mag.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                       \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                       \
} while (0)

static void ck_str(const char *got, const char *want, int line)
{
    g_checks++;
    if (!got || strcmp(got, want) != 0) {
        g_fail++;
        fprintf(stderr, "FALLA %s:%d  pick = <%s>\n  esperaba <%s>\n",
                __FILE__, line, got ? got : "(null)", want);
    }
}

static void test_pick_format(void)
{
    char out[256];

    /* 1700000000 = 2023-11-14T22:13:20Z */
    CK(bpq_pick_msg_format(out, sizeof(out), 1, 158, 0, 7,
                           "PB16", "HHZ", "CX", "", 'P', 1700000000.250, 0) == 0);
    ck_str(out, "1 158 0 7 PB16.HHZ.CX.-- ?0 20231114221320.250 0 0 0 P M -\n",
           __LINE__);

    /* net/loc nulos -> "--" (HYPOINVERSE no empareja la estacion con ellos). */
    CK(bpq_pick_msg_format(out, sizeof(out), 1, 158, 0, 8,
                           "PB16", "HHZ", NULL, NULL, 'S', 1700000000.250, 'M') == 0);
    ck_str(out, "1 158 0 8 PB16.HHZ.--.-- ?0 20231114221320.250 0 0 0 S M M\n",
           __LINE__);

    /* Segundos < 10: NO debe aparecer un espacio antes del punto. */
    CK(bpq_pick_msg_format(out, sizeof(out), 1, 158, 0, 9,
                           "PB16", "HHZ", "CX", "00", 'P', 1700000005.500, '-') == 0);
    ck_str(out, "1 158 0 9 PB16.HHZ.CX.00 ?0 20231114221325.500 0 0 0 P M -\n",
           __LINE__);

    /* Redondeo de ms que desborda el segundo: .9999 -> segundo siguiente. */
    CK(bpq_pick_msg_format(out, sizeof(out), 1, 158, 0, 10,
                           "PB16", "HHZ", "CX", "", 'P', 1700000000.9999, '-') == 0);
    ck_str(out, "1 158 0 10 PB16.HHZ.CX.-- ?0 20231114221321.000 0 0 0 P M -\n",
           __LINE__);

    /* Argumentos invalidos. */
    CK(bpq_pick_msg_format(NULL, 10, 1, 158, 0, 0, "A", "B", "C", "", 'P', 0, '-') == -1);
    CK(bpq_pick_msg_format(out, 0, 1, 158, 0, 0, "A", "B", "C", "", 'P', 0, '-') == -1);
    CK(bpq_pick_msg_format(out, sizeof(out), 1, 158, 0, 0, NULL, "B", "C", "", 'P', 0, '-') == -1);
}

/* Construye un TYPE_MAGNITUDE real con el motor y lo devuelve en `buf`. */
static int build_mag(char *buf, size_t n, double value, int nst)
{
    MAG_INFO m;
    memset(&m, 0, sizeof(m));
    snprintf(m.qid, sizeof(m.qid), "%010d", 1234);
    snprintf(m.szmagtype, sizeof(m.szmagtype), "%s", "ML");
    snprintf(m.algorithm, sizeof(m.algorithm), "%s", "CSNNet");
    m.imagtype = 1;
    m.mag = value;
    m.quality = 1.0;
    m.nstations = nst;
    m.origin_version = 163;
    return wr_mag_scnl(&m, buf, (int)n);
}

static void test_mag_parse(void)
{
    char raw[1024], raw2[1024], out[1024];
    char before[1024];
    size_t len;
    BpqMagMsg m, m2;

    CK(build_mag(raw, sizeof(raw), 3.4, 5) >= 0);
    len = strlen(raw);
    memcpy(before, raw, len + 1);

    CK(bpq_magmsg_parse(raw, len, &m) == 0);
    /* rd_mag parsea in-place: el llamante NO debe notar nada. */
    CK(memcmp(before, raw, len + 1) == 0);

    CK(m.qid == 1234);
    CK(m.mod == 163);
    CK(m.nsta == 5);
    CK(m.value > 3.39 && m.value < 3.41);
    CK(strcmp(m.type, "ML") == 0);
    CK(strcmp(m.algorithm, "CSNNet") == 0);

    /* Ida y vuelta por el emisor: "3.4-5" (formato de ws.c:1131). */
    m.mod = 163;
    CK(bpq_mag_msg_format(out, sizeof(out), &m, "3.4-5") == 0);
    CK(strlen(out) > 20);
    CK(bpq_magmsg_parse(out, strlen(out), &m2) == 0);
    CK(m2.qid == 1234);
    CK(m2.mod == 163);
    CK(m2.nsta == 5);
    CK(m2.value > 3.39 && m2.value < 3.41);
    CK(strcmp(m2.type, "ML") == 0);
    CK(strcmp(m2.algorithm, "CSNMan") == 0);   /* el emisor firma CSNMan */

    /* El formato exige un tipo de magnitud conocido. */
    m.type[0] = '\0';
    CK(bpq_mag_msg_format(out, sizeof(out), &m, "3.4-5") == -1);
    m.type[0] = 'M'; m.type[1] = 'L'; m.type[2] = '\0';
    CK(bpq_mag_msg_format(out, sizeof(out), NULL, "3.4-5") == -1);
    CK(bpq_mag_msg_format(out, sizeof(out), &m, "") == -1);
    CK(bpq_mag_msg_format(out, sizeof(out), &m, "sin-guion") == -1);

    /* Mensaje con varias entradas por estacion: el parseo no debe desbordar ni
       tocar el original (aqui solo miramos que no pete y que siga parseando). */
    {
        MAG_CHAN_INFO ch[8];
        char chan[4096];
        int i, n;
        memset(ch, 0, sizeof(ch));
        for (i = 0; i < 8; i++) {
            snprintf(ch[i].sta, sizeof(ch[i].sta), "PB%02d", i);
            snprintf(ch[i].comp, sizeof(ch[i].comp), "HHZ");
            snprintf(ch[i].net, sizeof(ch[i].net), "CX");
            ch[i].mag = 3.0 + 0.1 * i;
            ch[i].dist = 100.0;
        }
        n = wr_chan_mag(ch, 8, chan, sizeof(chan), 1);
        if (n > 0) {
            BpqMagMsg mm;
            memcpy(before, chan, (size_t)n + 1);
            CK(bpq_magmsg_parse(chan, (size_t)n, &mm) == 0);
            CK(memcmp(before, chan, (size_t)n + 1) == 0);
        }
    }
}

static void test_open_errors(void)
{
    BpqConfig cfg;
    BpqRings *r;
    char err[512];

    /* 1) Anillo de entrada inexistente: debe fallar ANTES de adjuntar. */
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.my_mod_name, sizeof(cfg.my_mod_name), "MOD_CSNHYPODBP");
    snprintf(cfg.in_ring, sizeof(cfg.in_ring), "BPQ_NO_EXISTE");
    snprintf(cfg.out_ring, sizeof(cfg.out_ring), "PICK_RING");
    cfg.heartbeat_s = 30;
    err[0] = '\0';
    r = bpq_rings_open(&cfg, err, sizeof(err));
    CK(r == NULL);
    CK(strstr(err, "BPQ_NO_EXISTE") != NULL);

    /* 2) Anillos OK pero modulo no declarado en earthworm.d. */
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.my_mod_name, sizeof(cfg.my_mod_name), "MOD_BPQ_NO_DECLARADO");
    snprintf(cfg.in_ring, sizeof(cfg.in_ring), "HYPO_RING_REF");
    snprintf(cfg.out_ring, sizeof(cfg.out_ring), "PICK_RING");
    snprintf(cfg.in_ring_pick, sizeof(cfg.in_ring_pick), "PICK_RING");
    cfg.heartbeat_s = 30;
    err[0] = '\0';
    r = bpq_rings_open(&cfg, err, sizeof(err));
    CK(r == NULL);
    CK(strstr(err, "earthworm.d") != NULL);
    if (r) bpq_rings_close(r);

    /* 3) Config nula. */
    CK(bpq_rings_open(NULL, err, sizeof(err)) == NULL);
    /* 4) Drenar sin anillos. */
    CK(bpq_rings_drain(NULL, NULL, NULL, NULL, NULL, err, sizeof(err)) == -1);
    CK(bpq_rings_heartbeat(NULL, 0.0) == 0);
    CK(bpq_rings_should_quit(NULL) == 0);
    CK(bpq_emit_pick(NULL, "A", "B", "C", "", 'P', 0.0, '-') == -1);
    CK(bpq_emit_mag(NULL, NULL, "1.0-1") == -1);
    CK(bpq_emit_confirm(NULL, NULL, NULL, 0) == -1);
}

int main(void)
{
    printf("== test_rings ==\n");
    test_pick_format();
    test_mag_parse();
    test_open_errors();
    printf("== rings checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
