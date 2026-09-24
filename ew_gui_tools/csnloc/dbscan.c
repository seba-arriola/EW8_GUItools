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

int DBSCAN_Cluster(const double *X, int n, int dim, double eps, int min_pts,
                   int *labels)
{
    int    *visited;
    int    *is_core;
    double  eps2;
    int     i, j, cid = 0;

    if (!X || !labels || n <= 0 || dim <= 0) return 0;
    if (eps <= 0.0) eps = 1.0;
    if (min_pts < 1) min_pts = 1;
    eps2 = eps * eps;

    visited = (int *)calloc((size_t)n, sizeof(int));
    is_core = (int *)calloc((size_t)n, sizeof(int));
    if (!visited || !is_core) { free(visited); free(is_core); return -1; }

    for (i = 0; i < n; i++) labels[i] = -1;

    /* Marcar puntos nucleo. */
    for (i = 0; i < n; i++) {
        int count = 0;
        for (j = 0; j < n; j++) {
            if (sq_dist(&X[i * dim], &X[j * dim], dim) <= eps2) count++;
        }
        is_core[i] = (count >= min_pts);
    }

    /* Expansión de clusters. */
    {
        int *stack = (int *)malloc(sizeof(int) * (size_t)n);
        if (!stack) { free(visited); free(is_core); return -1; }

    for (i = 0; i < n; i++) {
        int top = 0;

        if (visited[i] || !is_core[i]) continue;
        visited[i] = 1;
        labels[i] = cid;
        stack[top++] = i;

        while (top > 0) {
            int p = stack[--top];
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
    free(stack);
    }

    free(visited);
    free(is_core);
    return cid;
}
