#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/cfgfile.h"

void ewgui_cfgfile_free(EwCfgFile *cf)
{
    int i;
    if (!cf)
        return;
    for (i = 0; i < cf->nlines; i++) {
        EwCfgLine *l = &cf->lines[i];
        free(l->prefix);
        free(l->name);
        free(l->gap1);
        free(l->value);
        free(l->gap2);
        free(l->comment);
        free(l->raw);
    }
    free(cf->lines);
    cf->lines = NULL;
    cf->nlines = 0;
}

void ewgui_cfgfile_parse_line(char *line, EwCfgLine *l)
{
    char *p = line, *q;
    char *name, *vstart, *vaux;
    int in_q = 0;
    char *hash = NULL;

    memset(l, 0, sizeof(*l));
    l->prefix = strdup(p);
    l->prefix[0] = '\0';

    /* sangría inicial */
    p = line;
    while (*p == ' ' || *p == '\t') p++;
    strncpy(l->prefix, line, p - line);
    l->prefix[p - line] = '\0';

    /* línea vacía o comentario */
    if (*p == '\0') { l->kind = 2; l->raw = strdup(line); return; }
    if (*p == '#')  { l->kind = 1; l->raw = strdup(line); return; }

    /* name = primer token */
    name = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    l->name = malloc(p - name + 1);
    memcpy(l->name, name, p - name);
    l->name[p - name] = '\0';

    /* gap1 */
    q = p;
    while (*q == ' ' || *q == '\t') q++;
    l->gap1 = malloc(q - p + 1);
    memcpy(l->gap1, p, q - p);
    l->gap1[q - p] = '\0';

    /* value = hasta el primer '#' fuera de comillas */
    vstart = q;
    vaux = q;
    while (*vaux) {
        if (*vaux == '"') in_q = !in_q;
        else if (*vaux == '#' && !in_q) { hash = vaux; break; }
        vaux++;
    }
    if (hash) {
        q = hash;
        while (q > vstart && (q[-1] == ' ' || q[-1] == '\t')) q--;
        l->value = malloc(q - vstart + 1);
        memcpy(l->value, vstart, q - vstart);
        l->value[q - vstart] = '\0';
        l->gap2 = malloc(hash - q + 1);
        memcpy(l->gap2, q, hash - q);
        l->gap2[hash - q] = '\0';
        l->comment = strdup(hash);
    } else {
        char *vend = vstart + strlen(vstart);
        char *ve = vend;
        while (ve > vstart && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
        l->value = malloc(ve - vstart + 1);
        memcpy(l->value, vstart, ve - vstart);
        l->value[ve - vstart] = '\0';
        l->gap2 = malloc(vend - ve + 1);
        memcpy(l->gap2, ve, vend - ve);
        l->gap2[vend - ve] = '\0';
        l->comment = strdup("");
    }
    l->kind = 0;
}

int ewgui_cfgfile_read(const char *path, EwCfgFile *cf)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    int cap = 0;
    int last_nl = 1;

    if (!f)
        return -1;
    memset(cf, 0, sizeof(*cf));
    snprintf(cf->path, sizeof(cf->path), "%s", path);

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        last_nl = (nl != NULL);
        if (nl) *nl = '\0';
        if (cf->nlines == cap) {
            cap = cap ? cap * 2 : 32;
            EwCfgLine *n = realloc(cf->lines, cap * sizeof(EwCfgLine));
            if (!n) { fclose(f); ewgui_cfgfile_free(cf); return -1; }
            cf->lines = n;
        }
        ewgui_cfgfile_parse_line(line, &cf->lines[cf->nlines]);
        cf->nlines++;
    }
    cf->last_newline = last_nl;
    fclose(f);
    return 0;
}

static int copiar_archivo(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out;
    char buf[4096];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

int ewgui_cfgfile_write(EwCfgFile *cf)
{
    char bak[600], tmp[600];
    FILE *f;
    int i;

    snprintf(bak, sizeof(bak), "%s.bak", cf->path);
    if (copiar_archivo(cf->path, bak) != 0)
        return -1;

    snprintf(tmp, sizeof(tmp), "%s.tmp", cf->path);
    f = fopen(tmp, "w");
    if (!f)
        return -1;
    for (i = 0; i < cf->nlines; i++) {
        EwCfgLine *l = &cf->lines[i];
        if (l->kind == 0) {
            fputs(l->prefix, f);
            fputs(l->name, f);
            fputs(l->gap1, f);
            fputs(l->value, f);
            fputs(l->gap2, f);
            fputs(l->comment, f);
        } else {
            fputs(l->raw, f);
        }
        if (!(i == cf->nlines - 1 && !cf->last_newline))
            fputc('\n', f);
    }
    if (fclose(f) != 0)
        return -1;
    if (rename(tmp, cf->path) != 0)
        return -1;
    return 0;
}

void ewgui_cfgfile_resolve(const char *cfgfile, char *out, size_t n)
{
    const char *ep = getenv("EW_PARAMS");
    if (cfgfile[0] == '/') { snprintf(out, n, "%s", cfgfile); return; }
    if (ep && *ep) snprintf(out, n, "%s/%s", ep, cfgfile);
    else snprintf(out, n, "%s", cfgfile);
}
