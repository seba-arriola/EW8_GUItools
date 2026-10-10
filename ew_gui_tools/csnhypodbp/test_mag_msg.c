/*
 * test_mag_msg - contrato de transporte de TYPE_MAGNITUDE.
 *
 * El modulo ORIGEN de la solucion (csnloc 162 / hyp2000 163 / nlloc 164) viaja
 * en MAG_INFO.origin_version y rd_mag lo recupera: es lo que permite a
 * csnhypodbp asociar cada magnitud a la solucion (qid,mod) y no solo al qid.
 */
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include <rw_mag.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    MAG_INFO m, r;
    char buf[1024];

    setlocale(LC_NUMERIC, "C");
    memset(&m, 0, sizeof(m));
    snprintf(m.qid, sizeof(m.qid), "%s", "8210600000");
    snprintf(m.szmagtype, sizeof(m.szmagtype), "%s", "ML");
    snprintf(m.algorithm, sizeof(m.algorithm), "%s", "CSNNet");
    m.imagtype  = 1;
    m.mag       = 5.2;
    m.nstations = 10;
    m.origin_version = 163;   /* modulo origen: hyp2000 */

    CHECK(wr_mag(&m, buf, sizeof(buf)) == 0, "wr_mag serializa");

    memset(&r, 0, sizeof(r));   /* rd_mag usa pMagAux/size_aux antes de su memset */
    CHECK(rd_mag(buf, (int)strlen(buf), &r) == 0, "rd_mag parsea");
    CHECK(strcmp(r.qid, "8210600000") == 0, "qid viaja");
    CHECK(strcmp(r.szmagtype, "ML") == 0, "tipo viaja");
    CHECK(r.nstations == 10, "nstations viaja");
    CHECK(r.origin_version == 163, "origin_version = modulo origen (163)");

    /* Mismo qid, otro modulo => otro origin_version. */
    m.origin_version = 164;
    CHECK(wr_mag(&m, buf, sizeof(buf)) == 0, "wr_mag serializa (164)");
    memset(&r, 0, sizeof(r));
    CHECK(rd_mag(buf, (int)strlen(buf), &r) == 0, "rd_mag parsea (164)");
    CHECK(r.origin_version == 164, "origin_version = 164 (nlloc)");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_mag_msg\n");
    return 0;
}
