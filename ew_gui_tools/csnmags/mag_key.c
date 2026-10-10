#include "mag_key.h"

#include <stdio.h>

void mag_key(char *out, size_t n, const char *qid, int mod)
{
    if (!out || n == 0) return;
    snprintf(out, n, "%s|%d", qid ? qid : "", mod);
}
