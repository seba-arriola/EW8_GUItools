#include "csnhypodbp_mww_over.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnwphase.h"

/*
 * Overrides de la tabla del panel `Mww`: cada campo editado mueve SOLO su
 * miembro de la corrida, la banda derivada sigue a la magnitud y el t0 editado
 * recalcula el epoch (que es lo que usan la ventana de ondas y el i_master).
 */

static void fill_input(CsnwphaseInput *in)
{
    memset(in, 0, sizeof *in);
    in->year = 2015;
    in->month = 9;
    in->day = 16;
    in->hour = 22;
    in->minute = 54;
    in->second = 32.0;
    in->origin_epoch = csnwphase_epoch_utc(2015, 9, 16, 22, 54, 32.0);
    in->latitude = -31.573;
    in->longitude = -71.674;
    in->depth_km = 25.0;
    in->magnitude = 8.3;
    snprintf(in->event_id, sizeof in->event_id, "E1-20150916225432");
    snprintf(in->origin_id, sizeof in->origin_id, "E1-20150916225432:162");
}

static const char *band_of(const CsnwphaseCfg *cfg, const CsnwphaseInput *in)
{
    static char out[64];
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    int n = 0, i;

    memset(out, 0, sizeof out);
    assert(csnwphase_params_build(cfg, in, rows, CSNWPHASE_MAX_PARAMS, &n) == 0);
    for (i = 0; i < n; i++) {
        if (strcmp(rows[i].name, "banda de inversion [Hz]") == 0) {
            snprintf(out, sizeof out, "%s", rows[i].value);
            return out;
        }
    }
    assert(0 && "fila de banda ausente");
    return out;
}

int main(void)
{
    CsnwphaseCfg cfg, rcfg;
    CsnwphaseInput in, run;
    MwwOverrides ov;
    size_t i;

    printf("csnhypodbp: overrides de la tabla Mww\n");

    csnwphase_cfg_defaults(&cfg);
    fill_input(&in);

    /* Sin tocar nada, aplicar es inócuo: incluso si el .d se releyó con otros
     * valores DESPUES del init, los overrides no los pisan. */
    mww_over_init(&in, &cfg, &ov);
    assert(ov.touched == 0u);
    rcfg = cfg;
    rcfg.win_pre_sec = 111;
    run = in;
    run.origin_epoch = -1.0;
    mww_over_apply(&ov, &run, &rcfg);
    assert(rcfg.win_pre_sec == 111);
    assert(run.origin_epoch == -1.0);
    assert(run.magnitude == in.magnitude && run.latitude == in.latitude);

    /* Campo a campo: se mueve el suyo y solo el suyo. */
    {
        static const struct { int field; double value; } cases[] = {
            { CSNWPHASE_PF_LAT,       -20.5 },
            { CSNWPHASE_PF_LON,       -70.25 },
            { CSNWPHASE_PF_DEPTH,     123.0 },
            { CSNWPHASE_PF_WIN_PRE,   45.0 },
            { CSNWPHASE_PF_WIN_POST,  555.0 },
            { CSNWPHASE_PF_SCREEN_TH, 7.5 },
            { CSNWPHASE_PF_SCREEN_NR, 3.5 },
            { CSNWPHASE_PF_DMIN,      6.5 },
            { CSNWPHASE_PF_DMAX,      75.0 },
        };

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            mww_over_init(&in, &cfg, &ov);
            assert(mww_over_set(&ov, cases[i].field, cases[i].value) == 0);
            rcfg = cfg;
            run = in;
            mww_over_apply(&ov, &run, &rcfg);

            switch (cases[i].field) {
            case CSNWPHASE_PF_LAT:
                assert(run.latitude == cases[i].value);
                assert(run.longitude == in.longitude);
                break;
            case CSNWPHASE_PF_LON:
                assert(run.longitude == cases[i].value);
                assert(run.latitude == in.latitude);
                break;
            case CSNWPHASE_PF_DEPTH:
                assert(run.depth_km == cases[i].value);
                break;
            case CSNWPHASE_PF_WIN_PRE:
                assert(rcfg.win_pre_sec == (int)cases[i].value);
                assert(rcfg.win_post_sec == cfg.win_post_sec);
                break;
            case CSNWPHASE_PF_WIN_POST:
                assert(rcfg.win_post_sec == (int)cases[i].value);
                assert(rcfg.win_pre_sec == cfg.win_pre_sec);
                break;
            case CSNWPHASE_PF_SCREEN_TH:
                assert(rcfg.screen_th == cases[i].value);
                assert(rcfg.screen_nr == cfg.screen_nr);
                break;
            case CSNWPHASE_PF_SCREEN_NR:
                assert(rcfg.screen_nr == cases[i].value);
                assert(rcfg.screen_th == cfg.screen_th);
                break;
            case CSNWPHASE_PF_DMIN:
                assert(rcfg.dmin_deg == cases[i].value);
                assert(rcfg.dmax_deg == cfg.dmax_deg);
                break;
            case CSNWPHASE_PF_DMAX:
                assert(rcfg.dmax_deg == cases[i].value);
                assert(rcfg.dmin_deg == cfg.dmin_deg);
                break;
            default:
                assert(0 && "caso no cubierto");
            }
            /* El epoch del t0 NO se toca si no se editó la hora. */
            assert(run.origin_epoch == in.origin_epoch);
        }
    }

    /* La banda (derivada) sigue a la magnitud editada. */
    mww_over_init(&in, &cfg, &ov);
    assert(strcmp(band_of(&cfg, &in), "0.001 - 0.005") == 0);   /* mag 8.3 */
    assert(mww_over_set(&ov, CSNWPHASE_PF_MAGNITUDE, 6.0) == 0);
    rcfg = cfg;
    run = in;
    mww_over_apply(&ov, &run, &rcfg);
    assert(run.magnitude == 6.0);
    assert(strcmp(band_of(&rcfg, &run), "0.0067 - 0.02") == 0); /* mag 6.0 */
    assert(mww_over_set(&ov, CSNWPHASE_PF_MAGNITUDE, 7.2) == 0);
    mww_over_apply(&ov, &run, &rcfg);
    assert(strcmp(band_of(&rcfg, &run), "0.002 - 0.0083") == 0);

    /* La hora editada sí recalcula el epoch. */
    mww_over_init(&in, &cfg, &ov);
    assert(mww_over_set_time(&ov, "2015-09-16 23:00:00") == 0);
    run = in;
    mww_over_apply(&ov, &run, &cfg);
    assert(run.hour == 23 && run.minute == 0 && run.second == 0.0);
    assert(run.origin_epoch == csnwphase_epoch_utc(2015, 9, 16, 23, 0, 0.0));
    /* Se acepta el formato de la CLI (con 'T') y se rechaza la basura. */
    assert(mww_over_set_time(&ov, "2015-09-16T23:30:10") == 0);
    assert(ov.minute == 30 && ov.second == 10.0);
    assert(mww_over_set_time(&ov, "1230") == -1);
    assert(mww_over_set_time(&ov, "2015-09-16T99:00:00") == -1);
    assert(ov.minute == 30 && ov.second == 10.0);   /* no se aplicó a medias */

    /* DMIN/DMAX: se aplican solo si se tocan y `unset` los devuelve al origen. */
    {
        double d0 = cfg.dmin_deg;

        mww_over_init(&in, &cfg, &ov);
        assert(mww_over_set(&ov, CSNWPHASE_PF_DMIN, 12.0) == 0);
        assert(mww_over_set(&ov, CSNWPHASE_PF_DMAX, 88.0) == 0);
        run = in;
        rcfg = cfg;
        mww_over_apply(&ov, &run, &rcfg);
        assert(rcfg.dmin_deg == 12.0 && rcfg.dmax_deg == 88.0);

        /* Un texto invalido destoca el campo: vuelve al valor del .d, nunca a un
           prefijo tecleado a medias. */
        assert(mww_over_unset(&ov, CSNWPHASE_PF_DMIN) == 0);
        rcfg = cfg;
        mww_over_apply(&ov, &run, &rcfg);
        assert(rcfg.dmin_deg == d0 && rcfg.dmax_deg == 88.0);
        assert(mww_over_unset(&ov, CSNWPHASE_PF_DMIN) == 0);    /* idempotente */

        /* Los campos no numericos no se pueden destocar. */
        assert(mww_over_unset(&ov, CSNWPHASE_PF_NONE) == -1);
        assert(mww_over_unset(&ov, CSNWPHASE_PF_ORIGIN_TIME) == -1);
        assert(mww_over_unset(NULL, CSNWPHASE_PF_DMIN) == -1);
        rcfg = cfg;
        mww_over_apply(&ov, &run, &rcfg);
        assert(rcfg.dmin_deg == d0 && rcfg.dmax_deg == 88.0);
    }

    /* Rango y parseo de las cajas de texto (lo que sustituye al spin). */
    {
        double v = 0.0, lo = 0.0, hi = 0.0;
        static const struct { int field; const char *txt; int ok; double v; } cs[] = {
            { CSNWPHASE_PF_LAT,         "-31.573",  1, -31.573 },
            { CSNWPHASE_PF_LAT,         "-31,573",  1, -31.573 },  /* coma */
            { CSNWPHASE_PF_LAT,         "  -20.5 ", 1, -20.5   },  /* espacios */
            { CSNWPHASE_PF_DEPTH,       "123",      1, 123.0   },
            { CSNWPHASE_PF_MAGNITUDE,   "8.3",      1, 8.3     },
            { CSNWPHASE_PF_WIN_PRE,     "300",      1, 300.0   },
            { CSNWPHASE_PF_WIN_POST,    "900",      1, 900.0   },
            { CSNWPHASE_PF_SCREEN_TH,   "2",        1, 2.0     },
            { CSNWPHASE_PF_SCREEN_NR,   "2",        1, 2.0     },
            { CSNWPHASE_PF_DMIN,        "5.5",      1, 5.5     },
            { CSNWPHASE_PF_DMAX,        "90",       1, 90.0    },
            { CSNWPHASE_PF_LAT,         "95",       0, 0.0     },  /* fuera de rango */
            { CSNWPHASE_PF_DEPTH,       "-1",       0, 0.0     },
            { CSNWPHASE_PF_DMAX,        "181",      0, 0.0     },
            { CSNWPHASE_PF_LAT,         "",         0, 0.0     },
            { CSNWPHASE_PF_LAT,         "   ",      0, 0.0     },
            { CSNWPHASE_PF_LAT,         "abc",      0, 0.0     },
            { CSNWPHASE_PF_LAT,         "12abc",    0, 0.0     },
            { CSNWPHASE_PF_LAT,         "nan",      0, 0.0     },
            { CSNWPHASE_PF_LAT,         "inf",      0, 0.0     },
            { CSNWPHASE_PF_LAT,         "1e999",    0, 0.0     },  /* ERANGE */
            { CSNWPHASE_PF_NONE,        "1",        0, 0.0     },  /* no editable */
            { CSNWPHASE_PF_ORIGIN_TIME, "1",        0, 0.0     },
        };
        size_t k;

        for (k = 0; k < sizeof(cs) / sizeof(cs[0]); k++) {
            v = -12345.0;
            if (cs[k].ok) {
                assert(mww_field_parse(cs[k].field, cs[k].txt, &v) == 0);
                assert(v == cs[k].v);
            } else {
                assert(mww_field_parse(cs[k].field, cs[k].txt, &v) == -1);
                assert(v == -12345.0);          /* un texto malo no escribe nada */
            }
        }
        assert(mww_field_parse(CSNWPHASE_PF_LAT, NULL, &v) == -1);
        assert(mww_field_parse(CSNWPHASE_PF_LAT, "1", NULL) == 0);  /* solo validar */

        assert(mww_field_limits(CSNWPHASE_PF_LAT, &lo, &hi) == 0);
        assert(lo == -90.0 && hi == 90.0);
        assert(mww_field_limits(CSNWPHASE_PF_DMIN, &lo, &hi) == 0);
        assert(lo == 0.0 && hi == 180.0);
        assert(mww_field_limits(CSNWPHASE_PF_LAT, NULL, NULL) == 0);
        assert(mww_field_limits(CSNWPHASE_PF_NONE, NULL, NULL) == -1);
        assert(mww_field_limits(CSNWPHASE_PF_ORIGIN_TIME, NULL, NULL) == -1);
    }

    /* Los campos no editables se rechazan. */
    assert(mww_over_set(&ov, CSNWPHASE_PF_NONE, 1.0) == -1);
    assert(mww_over_set(&ov, CSNWPHASE_PF_ORIGIN_TIME, 1.0) == -1);

    printf("ALL CSNHYPODBP MWW OVER TESTS PASSED\n");
    return 0;
}
