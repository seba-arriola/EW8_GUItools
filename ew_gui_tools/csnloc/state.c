/******************************************************************************
 * state.c                                                                    *
 *                                                                            *
 * Persistencia del registro de eventos activos para RECUPERACION tras una    *
 * caida. csnloc es el UNICO escritor de este archivo; los consumidores        *
 * (csnhypodbp, csnrv) lo leen solo al arrancar. El display en tiempo real     *
 * siempre viene del anillo.                                                  *
 *                                                                            *
 * Formato de texto plano, una linea por evento:                              *
 *   E <id> <version> <t0> <lat> <lon> <depth> <nphases> <rms> <gap> <dmin>   *
 *     <score> <grid_level> <last_update_wall> <emitted> <nstored> <npruned>  *
 *     <t0_detect>                                                            *
 *   (<last_update_wall> es epoch wall-clock de la ultima deteccion; los      *
 *    state viejos traen ahi el t0 del candidato, que es un valor cercano y    *
 *    solo afecta al TTL tras el reinicio).                                    *
 *   P <sta> <net> <chan> <loc> <phase> <t_epoch> <weight> <residual> <man>   *
 *   X <sta> <net> <chan> <loc> <phase> <t_epoch> <weight> <man>              *
 *   (<man> = 0 automatica / 1 manual; opcional al leer por compatibilidad).   *
 *                                                                            *
 * Escritura atomica: se escribe a <path>.tmp y se renombra.                  *
 ******************************************************************************/
#include "csnloc.h"

int State_Save(const char *path, const EventRegistry *r)
{
    char  tmp[CSLOC_STR + 8];
    FILE *fp;
    int   i, j;

    if (!path || !r) return -1;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "w");
    if (!fp) return -1;

    for (i = 0; i < r->n; i++) {
        const EventRecord *e = &r->ev[i];

        fprintf(fp, "E %lu %u %.3f %.6f %.6f %.3f %d %.3f %.3f %.3f %.3f "
                    "%d %.3f %d %d %d %.3f\n",
                e->id, e->version, e->t0, e->lat, e->lon, e->depth_km,
                e->nphases, e->rms_sec, e->gap_deg, e->dmin_km, e->score,
                e->grid_level, e->last_update_wall, e->emitted,
                e->nphases_stored, e->npruned, e->t0_detect);

        for (j = 0; j < e->nphases_stored; j++) {
            const Pick *p = &e->phases[j];
            fprintf(fp, "P %s %s %s %s %d %.3f %d %.3f %d\n",
                    p->sta, p->net, p->chan, p->loc, p->phase,
                    p->t_epoch, p->weight, e->residual[j], p->manual ? 1 : 0);
        }
        for (j = 0; j < e->npruned; j++) {
            const Pick *p = &e->pruned[j];
            fprintf(fp, "X %s %s %s %s %d %.3f %d %d\n",
                    p->sta, p->net, p->chan, p->loc, p->phase,
                    p->t_epoch, p->weight, p->manual ? 1 : 0);
        }
    }

    fclose(fp);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int State_Load(const char *path, EventRegistry *r)
{
    FILE *fp;
    char  line[512];

    if (!path || !r) return -1;

    fp = fopen(path, "r");
    if (!fp) return 0;   /* sin archivo: registro vacio, no es error */

    EventRegistry_Init(r);

    while (fgets(line, sizeof(line), fp)) {
        char c = line[0];

        if (c == 'E') {
            EventRecord *e;
            if (r->n >= CSLOC_MAX_EVENTS) continue;
            e = &r->ev[r->n];
            memset(e, 0, sizeof(*e));
            if (sscanf(line + 2,
                       "%lu %u %lf %lf %lf %lf %d %lf %lf %lf %lf %d %lf %d %d %d %lf",
                       &e->id, &e->version, &e->t0, &e->lat, &e->lon,
                       &e->depth_km, &e->nphases, &e->rms_sec, &e->gap_deg,
                       &e->dmin_km, &e->score, &e->grid_level,
                       &e->last_update_wall, &e->emitted,
                       &e->nphases_stored, &e->npruned, &e->t0_detect) == 17) {
                /* Los contadores se reconstruyen con las lineas P/X. */
                e->nphases_stored = 0;
                e->npruned = 0;
                r->n++;
            }
        } else if (c == 'P') {
            EventRecord *e;
            Pick *p;
            double resid;
            if (r->n <= 0) continue;
            e = &r->ev[r->n - 1];
            if (e->nphases_stored >= CSLOC_MAX_PHASES) continue;
            p = &e->phases[e->nphases_stored];
            memset(p, 0, sizeof(*p));
            resid = 0.0;
            {
                int manual = 0;
                if (sscanf(line + 2, "%7s %3s %7s %3s %d %lf %d %lf %d",
                           p->sta, p->net, p->chan, p->loc, &p->phase,
                           &p->t_epoch, &p->weight, &resid, &manual) >= 8) {
                    p->manual = manual ? 1 : 0;
                    if (p->manual) e->has_manual = 1;
                    e->residual[e->nphases_stored] = resid;
                    e->nphases_stored++;
                }
            }
        } else if (c == 'X') {
            EventRecord *e;
            Pick *p;
            if (r->n <= 0) continue;
            e = &r->ev[r->n - 1];
            if (e->npruned >= CSLOC_MAX_PHASES) continue;
            p = &e->pruned[e->npruned];
            memset(p, 0, sizeof(*p));
            {
                int manual = 0;
                if (sscanf(line + 2, "%7s %3s %7s %3s %d %lf %d %d",
                           p->sta, p->net, p->chan, p->loc, &p->phase,
                           &p->t_epoch, &p->weight, &manual) >= 7) {
                    p->manual = manual ? 1 : 0;
                    if (p->manual) e->has_manual = 1;
                    e->npruned++;
                }
            }
        }
    }

    fclose(fp);
    return 0;
}
