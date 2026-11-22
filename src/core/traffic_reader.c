#include "traffic_reader.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 1024
#define INITIAL_CAP 256
#define INITIAL_BUCKETS 1024

static void ts_reset(TrafficSnapshot *s) {
    s->records = NULL;
    s->count = s->cap = 0;
    s->rows_read = s->rows_skipped = 0;
    s->fetched_at[0] = '\0';
}

/* Whole-string number parsing: rejects empty fields and trailing junk. */
static int parse_ll(const char *p, long long *out) {
    char *end;
    errno = 0;
    long long v = strtoll(p, &end, 10);
    if (end == p || *end != '\0' || errno == ERANGE) return 0;
    *out = v;
    return 1;
}

static int parse_double(const char *p, double *out) {
    char *end;
    errno = 0;
    double v = strtod(p, &end);
    if (end == p || *end != '\0' || errno == ERANGE || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

static char *trim(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' ||
                     p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = '\0';
    return p;
}

/* Splits "a,b,c,d" in place. Returns 1 if there are exactly 4 fields. */
static int split4(char *line, char *f[4]) {
    int n = 0;
    f[n++] = line;
    for (char *c = line; *c != '\0'; c++) {
        if (*c == ',') {
            if (n == 4) return 0;                 /* too many fields */
            *c = '\0';
            f[n++] = c + 1;
        }
    }
    if (n != 4) return 0;
    for (int i = 0; i < 4; i++) f[i] = trim(f[i]);
    return 1;
}

static int ts_append(TrafficSnapshot *s, const TrafficRecord *r) {
    if (s->count == s->cap) {
        int new_cap = s->cap == 0 ? INITIAL_CAP : s->cap * 2;
        TrafficRecord *p = (TrafficRecord *)realloc(s->records, (size_t)new_cap * sizeof(TrafficRecord));
        if (p == NULL) return -1;
        s->records = p;
        s->cap = new_cap;
    }
    s->records[s->count] = *r;
    if (ht_put(&s->index, r->edge_id, s->count) != 0) return -1;
    s->count++;
    return 0;
}

/* Handles one data row. Returns -1 only on out-of-memory. */
static int handle_row(TrafficSnapshot *s, char *line) {
    char *f[4];
    TrafficRecord r;
    s->rows_read++;

    if (!split4(line, f) || !parse_ll(f[0], &r.edge_id) ||
        !parse_double(f[1], &r.current_kph) || !parse_double(f[2], &r.freeflow_kph) ||
        !parse_double(f[3], &r.confidence) ||
        r.edge_id < 0 || r.current_kph < 0.0 || r.freeflow_kph <= 0.0 ||
        r.confidence < 0.0 || r.confidence > 1.0) {
        s->rows_skipped++;
        return 0;
    }
    if (r.confidence < TS_MIN_CONFIDENCE) { s->rows_skipped++; return 0; }
    if (ht_contains(&s->index, r.edge_id)) { s->rows_skipped++; return 0; }   /* duplicate */

    double t = 1.0 - r.current_kph / r.freeflow_kph;
    r.traffic_index = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return ts_append(s, &r);
}

/* "# fetched_at = 2026-11-10 09:30" -> copies the value. */
static void handle_comment(TrafficSnapshot *s, char *line) {
    char *p = trim(line + 1);                      /* skip '#' */
    const char key[] = "fetched_at";
    if (strncmp(p, key, sizeof key - 1) != 0) return;
    p = trim(p + sizeof key - 1);
    if (*p != '=') return;
    p = trim(p + 1);
    strncpy(s->fetched_at, p, TS_FETCHED_AT_LEN - 1);
    s->fetched_at[TS_FETCHED_AT_LEN - 1] = '\0';
}

int ts_load(TrafficSnapshot *s, const char *path) {
    ts_reset(s);
    if (ht_init(&s->index, INITIAL_BUCKETS) != 0) {
        s->index.buckets = NULL;
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;

    char line[LINE_MAX_LEN];
    int result = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            /* overlong line: discard the rest of it and count one skipped row */
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') { }
            s->rows_read++;
            s->rows_skipped++;
            continue;
        }
        char *p = trim(line);
        if (*p == '\0') continue;                              /* blank line */
        if (*p == '#') { handle_comment(s, p); continue; }
        if (strncmp(p, "edge_id", 7) == 0) continue;           /* header */
        if (handle_row(s, p) != 0) { result = -1; break; }     /* out of memory */
    }
    fclose(f);
    return result;
}

void ts_free(TrafficSnapshot *s) {
    free(s->records);
    ht_free(&s->index);
    ts_reset(s);
}

int ts_find(const TrafficSnapshot *s, long long edge_id, const TrafficRecord **out) {
    int pos;
    if (s->index.buckets == NULL || !ht_get(&s->index, edge_id, &pos)) return 0;
    if (out != NULL) *out = &s->records[pos];
    return 1;
}

double ts_traffic_index(const TrafficSnapshot *s, long long edge_id) {
    const TrafficRecord *r;
    return ts_find(s, edge_id, &r) ? r->traffic_index : 0.0;
}
