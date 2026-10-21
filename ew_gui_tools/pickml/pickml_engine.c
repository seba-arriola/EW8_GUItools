/******************************************************************************
 * pickml_engine.c — fábrica de motores: `stub` (tests) u `onnx` (producción).*
 *                                                                            *
 * El motor ONNX se construye sólo si el Makefile define PICKML_HAVE_ORT (y    *
 * entonces ort/onnxruntime_c_api.h debe estar en el include path). Sin ORT el *
 * módulo compila y TODOS los tests headless pasan: el motor real es un extra. *
 *                                                                            *
 * El stub es deliberadamente tonto pero determinista: busca el máximo |Z| de  *
 * la ventana y coloca P en él y S 12 s después. Sirve para verificar la       *
 * plomería (buffer, ventanas, dedup, JSON, tiempos) sin modelo.              *
 ******************************************************************************/

#include "pickml.h"

struct PmEngine {
    int  kind;
    void *h;          /* PmOrt* */
};

/* ---- backend ONNX (pickml_engine_onnx.c) ---- */
#ifdef PICKML_HAVE_ORT
int  pm_onnx_open(void **out, const char *ortlib, const char *model,
                  long nsamp, int threads, int optlevel, char *err, size_t errn);
int  pm_onnx_run(void *h, const float *in, int nwin, long nsamp,
                 float *p, float *s, float *det);
void pm_onnx_close(void *h);
#else
static int pm_onnx_open(void **out, const char *ortlib, const char *model,
                        long nsamp, int threads, int optlevel,
                        char *err, size_t errn)
{
    (void)out; (void)ortlib; (void)model; (void)nsamp; (void)threads; (void)optlevel;
    if (err) snprintf(err, errn,
        "ONNX Runtime no compilado: reconstruir con ORT_DIR=<dir de onnxruntime>");
    return PM_ERR;
}
static int pm_onnx_run(void *h, const float *in, int nwin, long nsamp,
                       float *p, float *s, float *det)
{
    (void)h; (void)in; (void)nwin; (void)nsamp; (void)p; (void)s; (void)det;
    return PM_ERR;
}
static void pm_onnx_close(void *h) { (void)h; }
#endif

/* ============================ stub ======================================== */

#define PM_STUB_MIN_COUNTS 100.0f   /* por debajo: ventana sin señal */

static int stub_run(const float *in, int nwin, long nsamp,
                    float *p, float *s, float *det)
{
    int w;

    if (nsamp < 8) return PM_ERR;

    for (w = 0; w < nwin; w++) {
        const float *z = in + (size_t)w * 3 * nsamp; /* primera comp = Z */
        float  *pw = p   + (size_t)w * nsamp;
        float  *sw = s   + (size_t)w * nsamp;
        float  *dw = det + (size_t)w * nsamp;
        long    i, best = -1;
        float   vmax = 0.0f;

        memset(pw, 0, sizeof(float) * (size_t)nsamp);
        memset(sw, 0, sizeof(float) * (size_t)nsamp);
        memset(dw, 0, sizeof(float) * (size_t)nsamp);

        for (i = 0; i < nsamp; i++) {
            float a = fabsf(z[i]);
            if (a > vmax) { vmax = a; best = i; }
        }
        if (best < 0 || vmax < PM_STUB_MIN_COUNTS) continue;

        pw[best] = 0.95f;
        dw[best] = 1.0f;
        if (best + 12 * 100 < nsamp) {
            sw[best + 12 * 100] = 0.90f;
            dw[best + 12 * 100] = 1.0f;
        }
    }
    return PM_OK;
}

/* ============================ API ========================================= */

int pm_engine_open(PmEngine **out, const PmConfig *c, char *err, size_t errn)
{
    PmEngine *e;

    if (!out || !c) return PM_ERR;
    *out = NULL;

    e = (PmEngine *)calloc(1, sizeof(*e));
    if (!e) {
        if (err) snprintf(err, errn, "sin memoria");
        return PM_ERR;
    }

    if (c->Engine == PM_ENGINE_STUB) {
        e->kind = PM_ENGINE_STUB;
        *out = e;
        return PM_OK;
    }

    e->kind = PM_ENGINE_ONNX;
    if (pm_onnx_open(&e->h, c->OrtLib, c->ModelFile, c->WindowSamples,
                     c->OrtThreads, c->OrtOptLevel, err, errn) != PM_OK) {
        free(e);
        return PM_ERR;
    }
    *out = e;
    return PM_OK;
}

int pm_engine_run(PmEngine *e, const float *in, int nwin, long nsamp,
                  float *p, float *s, float *det)
{
    if (!e || !in || nwin <= 0 || nsamp < 8 || !p || !s || !det) return PM_ERR;
    if (e->kind == PM_ENGINE_STUB) return stub_run(in, nwin, nsamp, p, s, det);
    return pm_onnx_run(e->h, in, nwin, nsamp, p, s, det);
}

void pm_engine_close(PmEngine *e)
{
    if (!e) return;
    if (e->kind == PM_ENGINE_ONNX) pm_onnx_close(e->h);
    free(e);
}

const char *pm_engine_name(const PmEngine *e)
{
    if (!e) return "none";
    return e->kind == PM_ENGINE_STUB ? "stub" : "onnx";
}
