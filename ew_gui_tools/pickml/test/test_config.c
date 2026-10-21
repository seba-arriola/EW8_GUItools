/* test_config.c — defaults, overrides y validaciones de pickml.d. */

#include "../pickml.h"
#include <unistd.h>

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

static char *write_cfg(const char *body)
{
    static char path[64];
    char tmpl[] = "/tmp/pickml_cfg_XXXXXX";
    int fd;
    FILE *f;

    fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    snprintf(path, sizeof(path), "%s", tmpl);
    f = fdopen(fd, "w");
    if (!f) { close(fd); return NULL; }
    fputs(body, f);
    fclose(f);
    return path;
}

int main(void)
{
    PmConfig c;
    char err[256];
    char *p;

    /* defaults de un fichero vacío */
    p = write_cfg("# vacío\n");
    CHECK(p != NULL);
    pm_config_defaults(&c);
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_OK);
    CHECK(c.WindowSamples == 6000);
    CHECK(fabs(c.Rate - 100.0) < 1e-9);
    CHECK(fabs(c.StepSec - 10.0) < 1e-9);
    CHECK(fabs(c.MinPosSec - 20.0) < 1e-9);
    CHECK(c.Engine == PM_ENGINE_ONNX);
    CHECK(c.WProb[0] > c.WProb[3]);

    /* overrides */
    p = write_cfg(
        "MyModuleId MOD_PICKML\n"
        "Engine stub\n"
        "StepSec 15\n"
        "MinPosSec 25\n"
        "PThreshold 0.42\n"
        "SThreshold 0.21\n"
        "DedupSec 5\n"
        "MaxGapSec 0.25\n"
        "PReportChan E\n"
        "SReportChan Z\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_OK);
    CHECK(c.Engine == PM_ENGINE_STUB);
    CHECK(fabs(c.StepSec - 15.0) < 1e-9);
    CHECK(fabs(c.MinPosSec - 25.0) < 1e-9);
    CHECK(fabs(c.PThreshold - 0.42) < 1e-9);
    CHECK(fabs(c.MaxGapSec - 0.25) < 1e-9);
    CHECK(c.PReportChan[0] == 'E');
    CHECK(c.SReportChan[0] == 'Z');

    /* la ventana es la nativa del modelo: 3001 (PhaseNet) es legítima */
    p = write_cfg("WindowSamples 3001\nStepSec 5\nMinPosSec 2\nBlindSamples 100\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_OK);
    CHECK(c.WindowSamples == 3001);

    /* una ventana absurda sigue siendo error */
    p = write_cfg("WindowSamples 4\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_ERR);
    CHECK(strstr(err, "WindowSamples") != NULL);

    /* claves de salida y etiqueta de modelo (partimos de defaults limpios:
     * un load fallido deja el struct a medias y no debe contaminar este caso) */
    pm_config_defaults(&c);
    p = write_cfg("ModelName phasenet\n"
                  "PickOutFile /tmp/p.jsonl\n"
                  "PicksOutFile /tmp/p.txt\n"
                  "RunMetaFile /tmp/p.meta\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_OK);
    CHECK(strcmp(c.ModelName, "phasenet") == 0);
    CHECK(strcmp(c.PickOutFile, "/tmp/p.jsonl") == 0);
    CHECK(strcmp(c.PicksOutFile, "/tmp/p.txt") == 0);
    CHECK(strcmp(c.RunMetaFile, "/tmp/p.meta") == 0);

    /* StepSec demasiado grande para no perder cobertura */
    p = write_cfg("StepSec 50\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_ERR);

    /* MinPosSec negativo */
    p = write_cfg("MinPosSec -1\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_ERR);

    /* weights no decrecientes */
    p = write_cfg("W0Prob 0.4\nW1Prob 0.7\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_ERR);

    /* canal de reporte inválido */
    p = write_cfg("PReportChan Q\n");
    CHECK(pm_config_load(p, &c, err, sizeof(err)) == PM_ERR);

    /* fichero inexistente */
    CHECK(pm_config_load("/tmp/pickml_no_existe.d", &c, err, sizeof(err)) == PM_ERR);

    if (fails) { fprintf(stderr, "test_config: %d fallos\n", fails); return 1; }
    printf("test_config: OK\n");
    return 0;
}
