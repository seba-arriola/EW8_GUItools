/******************************************************************************
 * dbscan.c                                                                   *
 *                                                                            *
 * DBSCAN sobre un conjunto de puntos en un espacio de dimensión dim.         *
 *                                                                            *
 * Se usa para agrupar las nucleaciones producidas por la back-projection:    *
 * picos cercanos en (lat, lon, depth, t0) se fusionan en un mismo evento,    *
 * mientras que eventos simultaneos en zonas distintas quedan separados.      *
 *                                                                            *
 * Implementacion O(n^2); n (numero de candidatos) es pequeno (<256).         *
 * labels[i] = -1 -> ruido; >=0 -> id de cluster.                             *
 ******************************************************************************/
#include "csnloc.h"
#include <pthread.h>

static int g_dbscan_threads = 1;

void DBSCAN_SetThreads(int nthreads)
{
    g_dbscan_threads = (nthreads > 0) ? nthreads : 1;
}

/* ------------------------------------------------------------------------- */
/* Diagnostico del DBSCAN. AssembleCandidates corre en varias grillas a la vez,*/
/* asi que estos contadores los escriben varios hilos: van con mutex.         */
/* Lo que decide si un indice espacial vale la pena es la relacion entre los   */
/* pares evaluados (n^2) y los que caen dentro de eps (`near`).                */
/* ------------------------------------------------------------------------- */
static pthread_mutex_t g_db_mx = PTHREAD_MUTEX_INITIALIZER;
static long      g_db_calls, g_db_sum_n, g_db_max_n;
static long long g_db_sum_n2, g_db_sum_near;
static long      g_db_hist[10];   /* n en 2^k: [0,2),[2,4),... [256,512), >=512 */

void DBSCAN_Stats(long *calls, long *sum_n, long long *sum_n2,
                  long long *sum_near, long *max_n, long *hist, int hn)
{
    int i;
    pthread_mutex_lock(&g_db_mx);
    if (calls)    *calls    = g_db_calls;
    if (sum_n)    *sum_n    = g_db_sum_n;
    if (sum_n2)   *sum_n2   = g_db_sum_n2;
    if (sum_near) *sum_near = g_db_sum_near;
    if (max_n)    *max_n    = g_db_max_n;
    if (hist) for (i = 0; i < hn && i < 10; i++) hist[i] = g_db_hist[i];
    pthread_mutex_unlock(&g_db_mx);
}

static double sq_dist(const double *a, const double *b, int dim)
{
    double s = 0.0;
    int    i;
    for (i = 0; i < dim; i++) {
        double d = a[i] - b[i];
        s += d * d;
    }
    return s;
}

/* La definicion completa del indice y de db_neighbors estan mas abajo (antes de
   DBSCAN_Cluster); aqui basta la declaracion adelantada. */
struct DbIndex;
static int db_neighbors(const struct DbIndex *ix, int i, double eps2, int dim,
                        int *out, int max_out);

/* Conteo de vecinos por punto, repartido por filas. Cada fila escribe solo su
   `is_core[i]`, asi que el resultado es identico con cualquier numero de hilos. */
typedef struct {
    const double  *X;
    const struct DbIndex *ix;   /* NULL = sin indice: barrido directo */
    int           *is_core;
    int            n, dim, i0, i1, min_pts;
    double         eps2;
    long           near;   /* pares dentro de eps de este rango (lo suma el hilo
                             principal: cada worker escribe solo el suyo) */
} CoreJob;

static void *core_worker(void *arg)
{
    CoreJob *j = (CoreJob *)arg;
    int      i, k;
    j->near = 0;
    for (i = j->i0; i < j->i1; i++) {
        int count;
        if (j->ix) {
            count = db_neighbors(j->ix, i, j->eps2, j->dim, NULL, 0);
            if (count < 0) count = 0;
        } else {
            count = 0;
            for (k = 0; k < j->n; k++)
                if (sq_dist(&j->X[i * j->dim], &j->X[k * j->dim], j->dim) <= j->eps2)
                    count++;
        }
        j->is_core[i] = (count >= j->min_pts);
        j->near += count;
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Indice espacial para el DBSCAN.                                            */
/*                                                                            */
/* El DBSCAN evalua n^2 pares cuando solo una fraccion minima esta dentro de  */
/* eps (medido: 5.3 %, con n de miles). El indice reparte los puntos en celdas */
/* de lado eps en las 4 dimensiones del espacio de features y cada punto solo */
/* recorre las 3^4 = 81 celdas vecinas.                                       */
/*                                                                            */
/* Exactitud: un vecino con |dx| <= eps en cada eje no puede estar a mas de   */
/* una celda de distancia, asi que las 81 celdas contienen TODOS los vecinos. */
/* El indice solo descarta candidatos: la comparacion final es exactamente el */
/* mismo `sq_dist(...) <= eps2`, asi que los conjuntos de vecinos son         */
/* identicos a los del barrido completo.                                      */
/* ------------------------------------------------------------------------- */
#define DB_HASH   (1 << 16)
#define DB_NBUF   8192   /* tope de vecinos por consulta; si se pasa, barrido directo */
#define DB_CSTR  4       /* coordenadas de celda guardadas por punto (<= dim) */

typedef struct DbIndex {
    const double *X;
    int          *head;   /* DB_HASH: primer indice de la celda, -1 si vacia */
    int          *next;   /* n: siguiente indice de la misma celda */
    int          *cell;   /* n*DB_CSTR: coordenadas de celda (dims 0..3) */
    int           n;
} DbIndex;

static unsigned db_hash4(int a, int b, int c, int d)
{
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)a) * 16777619u;
    h = (h ^ (unsigned)b) * 16777619u;
    h = (h ^ (unsigned)c) * 16777619u;
    h = (h ^ (unsigned)d) * 16777619u;
    return h & (DB_HASH - 1);
}

static int db_index_build(DbIndex *ix, const double *X, int n, int dim, double eps)
{
    int i, k;
    ix->X    = X;
    ix->n    = n;
    ix->head = (int *)malloc(sizeof(int) * DB_HASH);
    ix->next = (int *)malloc(sizeof(int) * (size_t)n);
    ix->cell = (int *)calloc((size_t)n * DB_CSTR, sizeof(int));
    if (!ix->head || !ix->next || !ix->cell) return -1;
    for (i = 0; i < DB_HASH; i++) ix->head[i] = -1;
    for (i = 0; i < n; i++) {
        int c[DB_CSTR] = { 0, 0, 0, 0 };
        for (k = 0; k < dim && k < DB_CSTR; k++)
            c[k] = (int)floor(X[i * dim + k] / eps);
        for (k = 0; k < DB_CSTR; k++)
            ix->cell[i * DB_CSTR + k] = c[k];
        k = (int)db_hash4(c[0], c[1], c[2], c[3]);
        ix->next[i] = ix->head[k];
        ix->head[k] = i;
    }
    return 0;
}

static void db_index_free(DbIndex *ix)
{
    free(ix->head); free(ix->next); free(ix->cell);
    ix->head = ix->next = ix->cell = NULL;
}

/* Numero de vecinos de `i` dentro de eps; si `out` no es NULL, deja sus indices.
   Devuelve -1 si no caben en `max_out` (el llamador usa entonces el barrido
   directo, que da lo mismo). */
static int db_neighbors(const struct DbIndex *ix, int i, double eps2, int dim,
                        int *out, int max_out)
{
    const int *ci = &ix->cell[i * DB_CSTR];
    const int  ns = (dim < DB_CSTR) ? dim : DB_CSTR;
    const double *xi = &ix->X[i * dim];
    int o0, o1, o2, o3, cnt = 0;

    for (o0 = -1; o0 <= 1; o0++) {
        if (ns < 1 && o0 != 0) continue;
        for (o1 = -1; o1 <= 1; o1++) {
            if (ns < 2 && o1 != 0) continue;
            for (o2 = -1; o2 <= 1; o2++) {
                if (ns < 3 && o2 != 0) continue;
                for (o3 = -1; o3 <= 1; o3++) {
                    int k, j, ok;
                    if (ns < 4 && o3 != 0) continue;
                    k = (int)db_hash4(ci[0] + o0, ci[1] + o1, ci[2] + o2,
                                      ci[3] + o3);
                    for (j = ix->head[k]; j != -1; j = ix->next[j]) {
                        const int *cj = &ix->cell[j * DB_CSTR];
                        ok = 1;
                        if (ns > 0 && cj[0] != ci[0] + o0) ok = 0;
                        if (ok && ns > 1 && cj[1] != ci[1] + o1) ok = 0;
                        if (ok && ns > 2 && cj[2] != ci[2] + o2) ok = 0;
                        if (ok && ns > 3 && cj[3] != ci[3] + o3) ok = 0;
                        if (!ok) continue;
                        if (sq_dist(xi, &ix->X[j * dim], dim) > eps2) continue;
                        if (out) {
                            if (cnt >= max_out) return -1;
                            out[cnt] = j;
                        }
                        cnt++;
                    }
                }
            }
        }
    }
    return cnt;
}

int DBSCAN_Cluster(const double *X, int n, int dim, double eps, int min_pts,
                   int *labels)
{
    int    *visited;
    int    *is_core;
    double  eps2;
    int     i, j, cid = 0;
    DbIndex ix;
    int     have_ix = 0;

    if (!X || !labels || n <= 0 || dim <= 0) return 0;
    if (eps <= 0.0) eps = 1.0;
    if (min_pts < 1) min_pts = 1;
    eps2 = eps * eps;

    /* Registro por llamada: lo que decide si un indice espacial vale la pena
       es cuantos pares evalua (n^2) frente a cuantos caen dentro de eps. */
    {
        int b = 0, v = n;
        while (v > 1 && b < 9) { v >>= 1; b++; }
        pthread_mutex_lock(&g_db_mx);
        g_db_calls++;
        g_db_sum_n  += n;
        g_db_sum_n2 += (long long)n * n;
        if (n > g_db_max_n) g_db_max_n = n;
        g_db_hist[b]++;
        pthread_mutex_unlock(&g_db_mx);
    }

    visited = (int *)calloc((size_t)n, sizeof(int));
    is_core = (int *)calloc((size_t)n, sizeof(int));
    if (!visited || !is_core) { free(visited); free(is_core); return -1; }

    /* Indice espacial: si falla la reserva, se sigue con el barrido directo. */
    have_ix = (db_index_build(&ix, X, n, dim, eps) == 0);

    for (i = 0; i < n; i++) labels[i] = -1;

    /* Marcar puntos nucleo: el conteo de vecinos de cada punto es independiente
       de los demas, asi que se reparte por filas entre hilos. */
    {
        CoreJob   jobs[64];
        pthread_t tids[64];
        int       nt = g_dbscan_threads, t, chunk;
        if (nt > 64) nt = 64;
        if (nt > n)  nt = n;
        if (nt < 1)  nt = 1;
        chunk = (n + nt - 1) / nt;
        for (t = 0; t < nt; t++) {
            jobs[t].X       = X;
            jobs[t].ix      = have_ix ? &ix : NULL;
            jobs[t].is_core = is_core;
            jobs[t].n       = n;
            jobs[t].dim     = dim;
            jobs[t].eps2    = eps2;
            jobs[t].min_pts = min_pts;
            jobs[t].i0      = t * chunk;
            jobs[t].i1      = jobs[t].i0 + chunk;
            if (jobs[t].i1 > n) jobs[t].i1 = n;
        }
        if (nt == 1) {
            core_worker(&jobs[0]);
        } else {
            for (t = 0; t < nt; t++)
                pthread_create(&tids[t], NULL, core_worker, &jobs[t]);
            for (t = 0; t < nt; t++)
                pthread_join(tids[t], NULL);
        }
        {
            long near = 0;
            for (t = 0; t < nt; t++) near += jobs[t].near;
            pthread_mutex_lock(&g_db_mx);
            g_db_sum_near += near;
            pthread_mutex_unlock(&g_db_mx);
        }
    }

    /* Expansión de clusters. */
    {
        int *stack = (int *)malloc(sizeof(int) * (size_t)n);
        int *nb    = (int *)malloc(sizeof(int) * DB_NBUF);
        if (!stack) {
            if (have_ix) db_index_free(&ix);
            free(visited); free(is_core); return -1;
        }

    for (i = 0; i < n; i++) {
        int top = 0;

        if (visited[i] || !is_core[i]) continue;
        visited[i] = 1;
        labels[i] = cid;
        stack[top++] = i;

        while (top > 0) {
            int p  = stack[--top];
            int nn = -1, m;

            if (have_ix && nb)
                nn = db_neighbors(&ix, p, eps2, dim, nb, DB_NBUF);

            if (nn >= 0) {
                /* Con min_pts > 1 hay puntos de borde y el orden de recorrido
                   decide a que nucleo se asignan, asi que se ordena igual que el
                   barrido original (ascendente). Con min_pts == 1 todos los
                   puntos son nucleo: los grupos son las componentes conexas y el
                   orden no influye, de modo que se evita el costo de ordenar
                   (que con n de miles se comeria la ganancia del indice). */
                if (min_pts > 1) {
                    for (m = 1; m < nn; m++) {
                        int key = nb[m], q = m - 1;
                        while (q >= 0 && nb[q] > key) { nb[q + 1] = nb[q]; q--; }
                        nb[q + 1] = key;
                    }
                }
                for (m = 0; m < nn; m++) {
                    j = nb[m];
                    if (!visited[j]) {
                        visited[j] = 1;
                        labels[j] = cid;
                        if (is_core[j]) stack[top++] = j;
                    }
                }
                continue;
            }

            for (j = 0; j < n; j++) {
                if (sq_dist(&X[p * dim], &X[j * dim], dim) > eps2) continue;
                if (!visited[j]) {
                    visited[j] = 1;
                    labels[j] = cid;
                    if (is_core[j]) stack[top++] = j;
                }
            }
        }
        cid++;
    }
    free(nb);
    free(stack);
    }

    if (have_ix) db_index_free(&ix);
    free(visited);
    free(is_core);
    return cid;
}
