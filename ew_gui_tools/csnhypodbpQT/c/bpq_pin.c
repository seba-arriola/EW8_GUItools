/* bpq_pin.c - Ver bpq_pin.h. */
#include "bpq_pin.h"

#include <stdio.h>
#include <string.h>

void bpq_pin_init(BpqPin *p)
{
    if (p) p->sol_key[0] = '\0';
}

void bpq_pin_clear(BpqPin *p)
{
    if (p) p->sol_key[0] = '\0';
}

int bpq_pin_set(BpqPin *p, const char *sol_key)
{
    size_t n;

    if (!p || !sol_key) return -1;
    n = strlen(sol_key);
    if (n == 0 || n >= BPQ_PIN_KEY_MAX) return -1;
    memcpy(p->sol_key, sol_key, n + 1);
    return 0;
}

int bpq_pin_active(const BpqPin *p)
{
    return (p && p->sol_key[0]) ? 1 : 0;
}

const char *bpq_pin_key(const BpqPin *p)
{
    return p ? p->sol_key : "";
}

int bpq_pin_matches(const BpqPin *p, const char *sol_key)
{
    if (!p || !sol_key || !p->sol_key[0]) return 0;
    return strcmp(p->sol_key, sol_key) == 0 ? 1 : 0;
}

int bpq_pin_reconcile(BpqPin *p, const char *const *keys, int n)
{
    int i;

    if (!bpq_pin_active(p)) return 0;
    for (i = 0; i < n; i++) {
        if (keys && keys[i] && strcmp(p->sol_key, keys[i]) == 0) return 1;
    }
    bpq_pin_clear(p);
    return 0;
}

int bpq_origin_key(int qid, int mod, unsigned version, char *out, size_t n)
{
    int rc;

    if (!out || n == 0) return -1;
    rc = snprintf(out, n, "%d:%d:%u", qid, mod, version);
    if (rc < 0 || (size_t)rc >= n) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}
