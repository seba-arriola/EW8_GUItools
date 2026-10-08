/******************************************************************************
 * polarization.c — análisis de polarización 3C (Jurkevics, 1988).            *
 *                                                                            *
 * A partir de la matriz de covarianza 3x3 de (E,N,Z) se obtienen los         *
 * autovalores/vectores y de ellos: rectilinealidad, planaridad, ángulo de    *
 * incidencia (desde la vertical), azimut y ratio H/V.                        *
 ******************************************************************************/

#include "pickS.h"

#define PICKS_PI 3.14159265358979323846

/* Jacobi cíclico para matriz simétrica 3x3. a[][] se destruye. */
static void jacobi3(double a[3][3], double eig[3], double vec[3][3])
{
    int    i, j, k, sweep;
    double b[3][3], v[3][3];

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            b[i][j] = a[i][j];
            v[i][j] = (i == j) ? 1.0 : 0.0;
        }

    for (sweep = 0; sweep < 50; sweep++) {
        double off = 0.0;
        for (i = 0; i < 3; i++)
            for (j = i + 1; j < 3; j++) off += b[i][j] * b[i][j];
        if (off < 1e-20) break;

        for (i = 0; i < 3; i++) {
            for (j = i + 1; j < 3; j++) {
                double theta, t, c, s, bii, bjj, bij;
                if (fabs(b[i][j]) < 1e-30) continue;
                theta = (b[j][j] - b[i][i]) / (2.0 * b[i][j]);
                t = (theta >= 0.0 ? 1.0 : -1.0) /
                    (fabs(theta) + sqrt(theta * theta + 1.0));
                c = 1.0 / sqrt(t * t + 1.0);
                s = t * c;

                bii = b[i][i]; bjj = b[j][j]; bij = b[i][j];
                b[i][i] = bii - t * bij;
                b[j][j] = bjj + t * bij;
                b[i][j] = b[j][i] = 0.0;

                for (k = 0; k < 3; k++) {
                    if (k != i && k != j) {
                        double bki = b[k][i], bkj = b[k][j];
                        b[k][i] = b[i][k] = c * bki - s * bkj;
                        b[k][j] = b[j][k] = s * bki + c * bkj;
                    }
                    {
                        double vki = v[k][i], vkj = v[k][j];
                        v[k][i] = c * vki - s * vkj;
                        v[k][j] = s * vki + c * vkj;
                    }
                }
            }
        }
    }

    for (i = 0; i < 3; i++) {
        eig[i] = b[i][i];
        for (j = 0; j < 3; j++) vec[i][j] = v[i][j];
    }
}

int PickS_Pol_Compute(const double *e, const double *n, const double *z,
                      int len, PickS_Pol *out)
{
    double C[3][3];
    double me = 0.0, mn = 0.0, mz = 0.0;
    double eig[3], vec[3][3];
    double tmp[3][3];
    int    i, j, order[3];
    double vmax;

    if (!e || !n || !z || !out || len < 3) return -1;

    for (i = 0; i < len; i++) { me += e[i]; mn += n[i]; mz += z[i]; }
    me /= len; mn /= len; mz /= len;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            C[i][j] = 0.0;

    for (i = 0; i < len; i++) {
        double de = e[i] - me, dn = n[i] - mn, dz = z[i] - mz;
        C[0][0] += de * de; C[0][1] += de * dn; C[0][2] += de * dz;
        C[1][1] += dn * dn; C[1][2] += dn * dz;
        C[2][2] += dz * dz;
    }
    C[1][0] = C[0][1];
    C[2][0] = C[0][2];
    C[2][1] = C[1][2];
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) C[i][j] /= len;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) tmp[i][j] = C[i][j];
    jacobi3(tmp, eig, vec);

    /* Orden descendente por autovalor. */
    for (i = 0; i < 3; i++) order[i] = i;
    for (i = 0; i < 2; i++)
        for (j = i + 1; j < 3; j++)
            if (eig[order[j]] > eig[order[i]]) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }

    out->lambda[0] = eig[order[0]];
    out->lambda[1] = eig[order[1]];
    out->lambda[2] = eig[order[2]];

    if (out->lambda[0] < 1e-20) {
        out->rectilinearity = 0.0;
        out->planarity = 0.0;
        out->incidence_deg = 0.0;
        out->azimuth_deg = 0.0;
        out->hv_ratio = 0.0;
        return 0;
    }

    out->rectilinearity = 1.0 - (out->lambda[1] + out->lambda[2]) /
                                (2.0 * out->lambda[0]);
    if (out->rectilinearity < 0.0) out->rectilinearity = 0.0;
    if (out->rectilinearity > 1.0) out->rectilinearity = 1.0;

    if (out->lambda[1] > 1e-20)
        out->planarity = 1.0 - out->lambda[2] / out->lambda[1];
    else
        out->planarity = 0.0;
    if (out->planarity < 0.0) out->planarity = 0.0;
    if (out->planarity > 1.0) out->planarity = 1.0;

    /* Vector propio principal (columna order[0] de vec). */
    {
        double ve = vec[0][order[0]];
        double vn = vec[1][order[0]];
        double vz = vec[2][order[0]];
        double norm = sqrt(ve * ve + vn * vn + vz * vz);
        if (norm < 1e-20) norm = 1.0;
        vmax = fabs(vz) / norm;
        if (vmax > 1.0) vmax = 1.0;
        out->incidence_deg = acos(vmax) * 180.0 / PICKS_PI;
        out->azimuth_deg = atan2(ve, vn) * 180.0 / PICKS_PI;
        if (out->azimuth_deg < 0.0) out->azimuth_deg += 360.0;
    }

    out->hv_ratio = (C[0][0] + C[1][1]) / (C[2][2] + 1e-20);

    return 0;
}
