/*
 * CA7: seleccion de grillas por picks.
 *   - grillas gruesas (global/regional) siempre activas.
 *   - grilla local activa solo si hay suficientes picks de sus estaciones
 *     asociadas (o una nucleacion gruesa cercana).
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static int make_grid(Grid *g, int level, double la0, double la1,
                     double lo0, double lo1, double node_km, double radius)
{
    memset(g, 0, sizeof(*g));
    g->level = level;
    g->lat_min = la0; g->lat_max = la1;
    g->lon_min = lo0; g->lon_max = lo1;
    g->node_km = node_km;
    g->depth_min = 0.0; g->depth_max = 100.0; g->depth_step = 50.0;
    g->sta_max_dist_km = radius;
    g->num_stations_per_node = 0;
    return Grid_BuildAxes(g);
}

static void add_pick(Pick *p, const StationList *st, const char *sta, double t)
{
    int i;
    memset(p, 0, sizeof(*p));
    snprintf(p->sta, sizeof(p->sta), "%s", sta);
    strcpy(p->net, "C"); strcpy(p->chan, "BHZ"); strcpy(p->loc, "--");
    p->phase = CSLOC_PHASE_P; strcpy(p->phase_name, "P");
    p->t_epoch = t; p->weight = 1;
    (void)st; (void)i;
}

int main(void)
{
    StationList  st;
    CSLocParams  cfg;
    Grid         gglobal, gnorth, gsouth;
    Pick         picks[8];
    int          pick_sidx[8];
    int          npick, i;

    if (Stations_Load("test/test_stations.txt", &st) != 0 &&
        Stations_Load("test_stations.txt", &st) != 0) {
        printf("FAIL: no se pudo cargar estaciones\n"); return 1;
    }

    if (make_grid(&gglobal, GRID_LEVEL_GLOBAL, -60, 10, -100, -30, 100, 400) != 0 ||
        make_grid(&gnorth,  GRID_LEVEL_LOCAL,  -26, -15, -76, -66, 20, 400) != 0 ||
        make_grid(&gsouth,  GRID_LEVEL_LOCAL,  -45, -30, -76, -66, 20, 400) != 0) {
        printf("FAIL: build grids\n"); return 1;
    }
    Grid_PrecomputeStations(&gglobal, &st);
    Grid_PrecomputeStations(&gnorth, &st);
    Grid_PrecomputeStations(&gsouth, &st);

    memset(&cfg, 0, sizeof(cfg));
    cfg.GridActivationMinPicks = 2;
    cfg.GridActivationMarginDeg = 0.5;

    /* Picks del norte. */
    add_pick(&picks[0], &st, "GO01", 1.8e9);
    add_pick(&picks[1], &st, "AP01", 1.8e9);
    add_pick(&picks[2], &st, "TA01", 1.8e9);
    add_pick(&picks[3], &st, "BO03", 1.8e9);
    npick = 4;
    for (i = 0; i < npick; i++) pick_sidx[i] = Stations_Find(&st, picks[i].sta);

    CHECK(Grid_IsActive(&gglobal, picks, pick_sidx, npick, &st, &cfg, NULL, 0) == 1,
          "CA7: global siempre activa");
    CHECK(Grid_IsActive(&gnorth, picks, pick_sidx, npick, &st, &cfg, NULL, 0) == 1,
          "CA7: grilla norte activa con picks del norte");
    CHECK(Grid_IsActive(&gsouth, picks, pick_sidx, npick, &st, &cfg, NULL, 0) == 0,
          "CA7: grilla sur inactiva con picks del norte");

    /* Picks del sur. */
    add_pick(&picks[0], &st, "FAR1", 1.8e9);
    add_pick(&picks[1], &st, "CO01", 1.8e9);
    npick = 2;
    for (i = 0; i < npick; i++) pick_sidx[i] = Stations_Find(&st, picks[i].sta);
    CHECK(Grid_IsActive(&gsouth, picks, pick_sidx, npick, &st, &cfg, NULL, 0) == 1,
          "CA7: grilla sur activa con picks del sur");
    CHECK(Grid_IsActive(&gnorth, picks, pick_sidx, npick, &st, &cfg, NULL, 0) == 0,
          "CA7: grilla norte inactiva con picks del sur");

    /* Nucleacion gruesa cerca del norte activa la grilla norte aunque no haya
       suficientes picks asociados. */
    {
        HypoCandidate nuc;
        memset(&nuc, 0, sizeof(nuc));
        nuc.lat = -20.0; nuc.lon = -69.0;
        npick = 0;
        CHECK(Grid_IsActive(&gnorth, picks, pick_sidx, npick, &st, &cfg, &nuc, 1) == 1,
              "CA7: nucleacion gruesa activa la grilla norte");
        nuc.lat = -40.0; nuc.lon = -72.0;
        CHECK(Grid_IsActive(&gnorth, picks, pick_sidx, npick, &st, &cfg, &nuc, 1) == 0,
              "CA7: nucleacion lejana no activa la grilla norte");
    }

    Grid_Free(&gglobal);
    Grid_Free(&gnorth);
    Grid_Free(&gsouth);

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_grid_activation\n");
    return 0;
}
