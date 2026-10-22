/* test_publish.c — política de publicación (pura, sin EarthWorm ni anillo). */

#include "../pickml.h"

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    PmConfig c;
    char     ph[8];
    int      ap, as;

    /* PublishPhases: sólo P, S, PS (y SP); cualquier otra cosa es error */
    ap = as = -1;
    CHECK(pm_publish_phases_parse("P", &ap, &as) == 0 && ap == 1 && as == 0);
    CHECK(pm_publish_phases_parse("s", &ap, &as) == 0 && ap == 0 && as == 1);
    CHECK(pm_publish_phases_parse("PS", &ap, &as) == 0 && ap == 1 && as == 1);
    CHECK(pm_publish_phases_parse("sp", &ap, &as) == 0 && ap == 1 && as == 1);
    CHECK(pm_publish_phases_parse("X", &ap, &as) != 0);
    CHECK(pm_publish_phases_parse("", &ap, &as) != 0);
    CHECK(pm_publish_phases_parse(NULL, &ap, &as) != 0);
    CHECK(pm_publish_phases_parse("PSS", &ap, &as) != 0);

    /* texto canónico */
    pm_publish_phases_str(1, 1, ph, sizeof(ph)); CHECK(strcmp(ph, "PS") == 0);
    pm_publish_phases_str(1, 0, ph, sizeof(ph)); CHECK(strcmp(ph, "P") == 0);
    pm_publish_phases_str(0, 1, ph, sizeof(ph)); CHECK(strcmp(ph, "S") == 0);
    pm_publish_phases_str(0, 0, ph, sizeof(ph)); CHECK(strcmp(ph, "-") == 0);

    /* decisión: fase primero, después el peso */
    memset(&c, 0, sizeof(c));
    c.PublishP = 1; c.PublishS = 1; c.PublishMaxWeight = 3;
    CHECK(pm_publish_decide(&c, 'P', 0) == PM_PUB_SEND);
    CHECK(pm_publish_decide(&c, 'S', 3) == PM_PUB_SEND);

    c.PublishS = 0;
    CHECK(pm_publish_decide(&c, 'S', 0) == PM_PUB_SKIP_PHASE);
    CHECK(pm_publish_decide(&c, 'P', 0) == PM_PUB_SEND);

    c.PublishP = 0; c.PublishS = 1;
    CHECK(pm_publish_decide(&c, 'P', 0) == PM_PUB_SKIP_PHASE);
    CHECK(pm_publish_decide(&c, 'S', 0) == PM_PUB_SEND);

    /* peso: umbral inclusivo (publica si weight <= PublishMaxWeight) */
    c.PublishP = c.PublishS = 1;
    c.PublishMaxWeight = 0;
    CHECK(pm_publish_decide(&c, 'P', 0) == PM_PUB_SEND);
    CHECK(pm_publish_decide(&c, 'P', 1) == PM_PUB_SKIP_WEIGHT);

    c.PublishMaxWeight = 2;
    CHECK(pm_publish_decide(&c, 'P', 2) == PM_PUB_SEND);
    CHECK(pm_publish_decide(&c, 'S', 3) == PM_PUB_SKIP_WEIGHT);

    if (fails) { fprintf(stderr, "test_publish: %d fallos\n", fails); return 1; }
    printf("test_publish: OK\n");
    return 0;
}
