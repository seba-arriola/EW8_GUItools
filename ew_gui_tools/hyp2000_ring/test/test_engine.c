/*
 * test_engine.c - Prueba de integracion del motor HYPOINVERSE vendorizado.
 *
 * Ejecuta la MISMA secuencia de comandos que hyp2000_ring (sin anillos) sobre
 * un ARC de entrada, y comprueba que se produce un ARC de salida.
 *
 * Uso:  test_engine <fichero.hyp> <arc_entrada> [arc_salida]
 * Se ejecuta en un directorio con el .hyp, el .sta y el .crh del modelo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void hypoinv_wrapper(const char *cmd, int *ret);

static int fails = 0;

static int call(const char *c)
{
    int r = 0;
    hypoinv_wrapper(c, &r);
    printf("cmd %-22s ret=%d\n", c, r);
    if (r != 1) fails++;
    return (r == 1) ? 0 : -1;
}

int main(int argc, char **argv)
{
    char  msg[256];
    char  arc[65536];
    FILE *fp;
    size_t n;

    if (argc < 3) { fprintf(stderr, "Uso: %s <hyp> <arcin> [arcout]\n", argv[0]); return 1; }

    snprintf(msg, sizeof(msg), "@%s", argv[1]);
    if (call(msg))               return 1;
    if (call("200 T 1900 0"))    return 1;
    if (call("COP 5"))           return 1;
    if (call("PHS 'arcIn'"))     return 1;
    if (call("CAR 3"))           return 1;
    if (call("ARC 'arcOut'"))    return 1;
    if (call("SUM 'none'"))      return 1;

    fp = fopen("arcIn", "w");
    if (!fp) { perror("arcIn"); return 1; }
    {
        FILE *fi = fopen(argv[2], "r");
        if (!fi) { perror(argv[2]); return 1; }
        n = fread(arc, 1, sizeof(arc) - 1, fi);
        fclose(fi);
    }
    fwrite(arc, 1, n, fp);
    fclose(fp);
    printf("arcIn: %zu bytes\n", n);

    if (call("LOC")) { printf("LOC fallo\n"); return 1; }

    fp = fopen("arcOut", "r");
    if (!fp) { printf("FAIL: no se genero arcOut\n"); return 1; }
    n = fread(arc, 1, sizeof(arc) - 1, fp);
    fclose(fp);
    arc[n] = '\0';
    printf("arcOut: %zu bytes\n", n);

    if (n == 0) { printf("FAIL: arcOut vacio\n"); fails++; }
    else {
        char *p = strchr(arc, '\n');
        int   l1 = p ? (int)(p - arc) : (int)n;
        printf("=== arcOut linea 1 (%d chars) ===\n%.*s\n", l1, l1, arc);
    }

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_engine\n");
    return 0;
}
