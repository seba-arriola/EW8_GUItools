/******************************************************************************
 * pickml_engine_onnx.c — motor ONNX Runtime (sólo con ORT_DIR).              *
 *                                                                            *
 * Se compila únicamente si el Makefile define PICKML_HAVE_ORT e incluye el    *
 * header de ONNX Runtime:
 *                                                                            *
 *   make ORT_DIR=/ruta/a/onnxruntime                                      *
 *                                                                            *
 * La librería se carga con dlopen (sólo hace falta UN símbolo, OrtGetApiBase),*
 * así que el binario base no tiene dependencia de enlace con ORT.            *
 *                                                                            *
 * ESTADO: escrito pero NO verificado en esta máquina (no hay ONNX Runtime ni *
 * modelo). La primera verificación es `make check-engine` con ORT_DIR y el    *
 * fixture dorado; hasta entonces trátese como no probado.                    *
 ******************************************************************************/

#ifdef PICKML_HAVE_ORT

#include "pickml.h"

#include <dlfcn.h>
#include <onnxruntime_c_api.h>

#define PM_ORT_API_VERSION ORT_API_VERSION

typedef const OrtApiBase *(*OrtGetApiBaseFn)(void);

struct PmOrt {
    void              *lib;
    const OrtApi      *api;
    OrtEnv            *env;
    OrtSessionOptions *opts;
    OrtSession        *sess;
    OrtAllocator      *alloc;
    OrtMemoryInfo     *mem;
    char              *in_name;
    char              *out_name;
};

static int ort_fail(char *err, size_t errn, const OrtApi *api, OrtStatus *st,
                    const char *what)
{
    if (!st) return PM_OK;          /* la llamada tuvo éxito */

    snprintf(err, errn, "%s: %s", what, api->GetErrorMessage(st));
    api->ReleaseStatus(st);
    return PM_ERR;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

#define ORT_TRY(api, err, errn, what, call) do {                     \
    OrtStatus *_st = (call);                                         \
    if (_st) return ort_fail(err, errn, api, _st, what);             \
} while (0)

static void pm_ort_free(struct PmOrt *o)
{
    if (!o) return;
    if (o->api) {
        if (o->out_name && o->alloc) o->alloc->Free(o->alloc, o->out_name);
        if (o->in_name  && o->alloc) o->alloc->Free(o->alloc, o->in_name);
        if (o->mem)  o->api->ReleaseMemoryInfo(o->mem);
        if (o->sess) o->api->ReleaseSession(o->sess);
        if (o->opts) o->api->ReleaseSessionOptions(o->opts);
        if (o->env)  o->api->ReleaseEnv(o->env);
    }
    if (o->lib) dlclose(o->lib);
    free(o);
}

int pm_onnx_open(void **out, const char *ortlib, const char *model,
                 long nsamp, int threads, int optlevel, char *err, size_t errn)
{
    struct PmOrt  *o;
    OrtGetApiBaseFn get_base;
    OrtTypeInfo   *ti = NULL;
    const OrtTensorTypeAndShapeInfo *ts = NULL;
    GraphOptimizationLevel opt = ORT_ENABLE_BASIC;
    int64_t dims[8];
    size_t  ndim = 0;
    int     rc;
    double  t0ms = now_ms(), t_dl = 0, t_api = 0, t_opts = 0, t_sess = 0;

    if (!out) return PM_ERR;
    *out = NULL;
    if (err && errn) err[0] = '\0';

    if (!ortlib || !ortlib[0]) {
        if (err) snprintf(err, errn, "OrtLib no configurado en el .d");
        return PM_ERR;
    }
    if (!model || !model[0]) {
        if (err) snprintf(err, errn, "ModelFile no configurado en el .d");
        return PM_ERR;
    }

    o = (struct PmOrt *)calloc(1, sizeof(*o));
    if (!o) {
        if (err) snprintf(err, errn, "sin memoria");
        return PM_ERR;
    }

    o->lib = dlopen(ortlib, RTLD_NOW | RTLD_LOCAL);
    if (!o->lib) {
        if (err) snprintf(err, errn, "dlopen(%s): %s", ortlib, dlerror());
        pm_ort_free(o);
        return PM_ERR;
    }

    *(void **)(&get_base) = dlsym(o->lib, "OrtGetApiBase");
    t_dl = now_ms();
    if (!get_base) {
        if (err) snprintf(err, errn, "%s no expone OrtGetApiBase", ortlib);
        pm_ort_free(o);
        return PM_ERR;
    }

    o->api = get_base()->GetApi(PM_ORT_API_VERSION);
    if (!o->api) {
        if (err) snprintf(err, errn,
            "OrtGetApiBase no soporta la version %d de la API", PM_ORT_API_VERSION);
        pm_ort_free(o);
        return PM_ERR;
    }

    rc = ort_fail(err, errn, o->api,
                  o->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "pickml", &o->env),
                  "CreateEnv");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    switch (optlevel) {
        case 0:  opt = ORT_DISABLE_ALL; break;
        case 1:  opt = ORT_ENABLE_BASIC; break;
        default: opt = ORT_ENABLE_ALL;   break;
    }
    t_api = now_ms();

    rc = ort_fail(err, errn, o->api, o->api->CreateSessionOptions(&o->opts),
                  "CreateSessionOptions");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }
    t_opts = now_ms();

    rc = ort_fail(err, errn, o->api,
                  o->api->SetSessionGraphOptimizationLevel(o->opts, opt),
                  "SetSessionGraphOptimizationLevel");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    if (threads > 0) {
        rc = ort_fail(err, errn, o->api,
                      o->api->SetIntraOpNumThreads(o->opts, threads),
                      "SetIntraOpNumThreads");
        if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }
    }

    rc = ort_fail(err, errn, o->api,
                  o->api->CreateSession(o->env, model, o->opts, &o->sess),
                  "CreateSession");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }
    t_sess = now_ms();

    rc = ort_fail(err, errn, o->api, o->api->GetAllocatorWithDefaultOptions(&o->alloc),
                  "GetAllocatorWithDefaultOptions");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    rc = ort_fail(err, errn, o->api, o->api->SessionGetInputName(o->sess, 0, o->alloc, &o->in_name),
                  "SessionGetInputName");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    rc = ort_fail(err, errn, o->api, o->api->SessionGetOutputName(o->sess, 0, o->alloc, &o->out_name),
                  "SessionGetOutputName");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    /* El grafo debe aceptar [B, 3, 6000]: si no, mejor fallar al arrancar que
     * publicar basura (misma lección que el resto del repo). */
    rc = ort_fail(err, errn, o->api, o->api->SessionGetInputTypeInfo(o->sess, 0, &ti),
                  "SessionGetInputTypeInfo");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    ts = NULL;
    rc = ort_fail(err, errn, o->api,
                  o->api->CastTypeInfoToTensorInfo(ti, &ts),
                  "CastTypeInfoToTensorInfo");
    if (rc != PM_OK) {
        o->api->ReleaseTypeInfo(ti);
        pm_ort_free(o);
        return PM_ERR;
    }
    if (!ts) {
        if (err) snprintf(err, errn, "la entrada del grafo no es un tensor");
        o->api->ReleaseTypeInfo(ti);
        pm_ort_free(o);
        return PM_ERR;
    }

    rc = ort_fail(err, errn, o->api, o->api->GetDimensionsCount(ts, &ndim),
                  "GetDimensionsCount");
    if (rc == PM_OK)
        rc = ort_fail(err, errn, o->api, o->api->GetDimensions(ts, dims, 8),
                      "GetDimensions");
    o->api->ReleaseTypeInfo(ti);
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    if (ndim != 3 || dims[1] != 3 || dims[2] != nsamp) {
        if (err) snprintf(err, errn,
            "el grafo espera [B,%lld,%lld] y pickml usa [B,3,%ld] "
            "(WindowSamples debe ser la ventana nativa del modelo)",
            (long long)dims[1], (long long)dims[2], nsamp);
        pm_ort_free(o);
        return PM_ERR;
    }

    rc = ort_fail(err, errn, o->api,
                  o->api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &o->mem),
                  "CreateCpuMemoryInfo");
    if (rc != PM_OK) { pm_ort_free(o); return PM_ERR; }

    fprintf(stderr,
        "pickml: onnx arranque: dlopen=%.0f api+env=%.0f opts=%.0f "
        "sesion=%.0f total=%.0f ms\n",
        t_dl - t0ms, t_api - t_dl, t_opts - t_api,
        t_sess - t_opts, now_ms() - t0ms);

    *out = o;
    return PM_OK;
}

int pm_onnx_run(void *h, const float *in, int nwin, long nsamp,
                float *p, float *s, float *det)
{
    struct PmOrt *o = (struct PmOrt *)h;
    int64_t       shape[3];
    OrtValue     *tin = NULL, *tout = NULL;
    const char   *in_names[1], *out_names[1];
    const OrtValue *in_vals[1];
    OrtStatus    *st;
    float        *data = NULL;
    size_t        count = 0;
    int           i, c, w;
    int           rc = PM_ERR;

    if (!o || !o->api || !in || nwin <= 0) return PM_ERR;

    shape[0] = nwin;
    shape[1] = 3;
    shape[2] = nsamp;

    st = o->api->CreateTensorWithDataAsOrtValue(o->mem, (void *)in,
             (size_t)nwin * 3 * (size_t)nsamp * sizeof(float),
             shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tin);
    if (st) { o->api->ReleaseStatus(st); return PM_ERR; }

    in_names[0]  = o->in_name;
    out_names[0] = o->out_name;
    in_vals[0]   = tin;

    st = o->api->Run(o->sess, NULL, in_names, in_vals, 1, out_names, 1, &tout);
    if (st) {
        fprintf(stderr, "pickml: ORT Run: %s\n", o->api->GetErrorMessage(st));
        o->api->ReleaseStatus(st);
        o->api->ReleaseValue(tin);
        return PM_ERR;
    }

    st = o->api->GetTensorMutableData(tout, (void **)&data);
    if (st) { o->api->ReleaseStatus(st); goto done; }

    {
        OrtTensorTypeAndShapeInfo *info = NULL;
        int64_t d[8];
        size_t  nd = 0, k;

        st = o->api->GetTensorTypeAndShape(tout, &info);
        if (st) { o->api->ReleaseStatus(st); goto done; }

        /* No se usa GetTensorShapeElementCount: con el eje de batch dinámico
         * puede caer en SafeInt (dim -1) en vez de devolver la forma real. */
        st = o->api->GetDimensionsCount(info, &nd);
        if (st || nd == 0 || nd > 8) {
            if (st) o->api->ReleaseStatus(st);
            o->api->ReleaseTensorTypeAndShapeInfo(info);
            fprintf(stderr, "pickml: la salida del grafo no tiene dimensiones legibles\n");
            goto done;
        }
        st = o->api->GetDimensions(info, d, nd);
        o->api->ReleaseTensorTypeAndShapeInfo(info);
        if (st) { o->api->ReleaseStatus(st); goto done; }

        count = 1;
        for (k = 0; k < nd; k++) {
            if (d[k] <= 0) {
                fprintf(stderr, "pickml: salida del grafo con dimension dinamica (%lld)\n",
                        (long long)d[k]);
                goto done;
            }
            count *= (size_t)d[k];
        }
    }

    if (count != (size_t)nwin * (size_t)nsamp * 3) {
        fprintf(stderr, "pickml: salida del grafo con %zu elementos (esperaba %zu)\n",
                count, (size_t)nwin * (size_t)nsamp * 3);
        goto done;
    }

    /* [nwin][nsamp][3] -> tres curvas [nwin][nsamp] */
    for (w = 0; w < nwin; w++) {
        const float *base = data + (size_t)w * (size_t)nsamp * 3;
        for (i = 0; i < (int)nsamp; i++) {
            p[(size_t)w * (size_t)nsamp + i] = base[i * 3 + 0];
            s[(size_t)w * (size_t)nsamp + i] = base[i * 3 + 1];
            det[(size_t)w * (size_t)nsamp + i] = base[i * 3 + 2];
        }
    }
    (void)c;
    rc = PM_OK;

done:
    if (tout) o->api->ReleaseValue(tout);
    o->api->ReleaseValue(tin);
    return rc;
}

void pm_onnx_close(void *h)
{
    pm_ort_free((struct PmOrt *)h);
}

#endif /* PICKML_HAVE_ORT */
