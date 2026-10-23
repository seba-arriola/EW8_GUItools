/*
 * test_pin.c - Nucleo del origen fijado: formato de clave, set/replace/clear,
 * matches y reconciliacion cuando la solucion desaparece.
 */
#include <stdio.h>
#include <string.h>

#include "bpq_pin.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                       \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                       \
} while (0)

int main(void)
{
    BpqPin p;
    char key[BPQ_PIN_KEY_MAX];
    char big[BPQ_PIN_KEY_MAX + 8];
    const char *keys_present[] = { "100:162:1", "100:163:2" };
    const char *keys_other[] = { "100:164:3", "200:162:1" };

    /* --- formato de clave ------------------------------------------------ */
    bpq_origin_key(100, 163, 2, key, sizeof(key));
    CK(strcmp(key, "100:163:2") == 0);
    CK(bpq_origin_key(100, 163, 2, NULL, sizeof(key)) == -1);
    CK(bpq_origin_key(100, 163, 2, key, 3) == -1);
    CK(key[0] == '\0');

    /* --- estado ---------------------------------------------------------- */
    bpq_pin_init(&p);
    CK(!bpq_pin_active(&p));
    CK(strcmp(bpq_pin_key(&p), "") == 0);

    CK(bpq_pin_set(&p, "") == -1);
    CK(bpq_pin_set(&p, NULL) == -1);
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CK(bpq_pin_set(&p, big) == -1);
    CK(!bpq_pin_active(&p));

    CK(bpq_pin_set(&p, "100:162:1") == 0);
    CK(bpq_pin_active(&p));
    CK(strcmp(bpq_pin_key(&p), "100:162:1") == 0);
    CK(bpq_pin_matches(&p, "100:162:1"));
    CK(!bpq_pin_matches(&p, "100:163:2"));

    /* Reemplazo: solo fijando otro origen. */
    CK(bpq_pin_set(&p, "100:163:2") == 0);
    CK(strcmp(bpq_pin_key(&p), "100:163:2") == 0);
    CK(!bpq_pin_matches(&p, "100:162:1"));

    /* --- reconciliacion -------------------------------------------------- */
    CK(bpq_pin_reconcile(&p, keys_present, 2) == 1);
    CK(bpq_pin_active(&p));
    CK(bpq_pin_reconcile(&p, keys_other, 2) == 0);
    CK(!bpq_pin_active(&p));
    CK(strcmp(bpq_pin_key(&p), "") == 0);

    /* Sin pin: reconcile no inventa estado. */
    CK(bpq_pin_reconcile(&p, keys_present, 2) == 0);
    CK(!bpq_pin_active(&p));

    /* Lista vacia/NULL limpia un pin activo. */
    CK(bpq_pin_set(&p, "100:162:1") == 0);
    CK(bpq_pin_reconcile(&p, NULL, 0) == 0);
    CK(!bpq_pin_active(&p));

    bpq_pin_clear(&p);
    CK(!bpq_pin_active(&p));

    printf("== pin checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
