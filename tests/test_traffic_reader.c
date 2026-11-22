#include "../src/core/traffic_reader.h"
#include "test_util.h"

#include <math.h>
#include <string.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static void test_valid(void) {
    TrafficSnapshot s;
    CHECK(ts_load(&s, "tests/fixtures/traffic_valid.csv") == 0);
    CHECK(s.rows_read == 5 && s.rows_skipped == 0 && s.count == 5);
    CHECK(strcmp(s.fetched_at, "2026-11-10 09:30") == 0);

    const TrafficRecord *r = NULL;
    CHECK(ts_find(&s, 1452, &r) == 1 && r != NULL);
    CHECK(r->edge_id == 1452 && near(r->current_kph, 33) && near(r->freeflow_kph, 42));
    CHECK(near(r->confidence, 0.60));
    CHECK(near(r->traffic_index, 1.0 - 33.0 / 42.0));

    CHECK(near(ts_traffic_index(&s, 7), 0.5));      /* half of free-flow speed   */
    CHECK(near(ts_traffic_index(&s, 9), 0.0));      /* at free flow; confidence exactly 0.5 is accepted */
    CHECK(near(ts_traffic_index(&s, 12), 0.0));     /* faster than free flow: clamped to 0 */
    CHECK(near(ts_traffic_index(&s, 15), 1.0));     /* standstill */

    /* edges without data are free flow */
    CHECK(ts_find(&s, 999, &r) == 0);
    CHECK(ts_find(&s, 999, NULL) == 0);
    CHECK(near(ts_traffic_index(&s, 999), 0.0));
    ts_free(&s);
}

static void test_messy(void) {
    TrafficSnapshot s;
    CHECK(ts_load(&s, "tests/fixtures/traffic_messy.csv") == 0);
    /* 12 data rows; only ids 1 and 9 are valid (ids 2..8, the duplicate 1,
     * -3 and the empty id are rejected). */
    CHECK(s.rows_read == 12);
    CHECK(s.rows_skipped == 10);
    CHECK(s.count == 2);
    CHECK(strcmp(s.fetched_at, "2026-11-10 09:30") == 0);   /* "#fetched_at=..." without spaces */

    const TrafficRecord *r;
    CHECK(ts_find(&s, 1, &r) && near(r->traffic_index, 0.25));   /* first row kept, not the duplicate */
    CHECK(ts_find(&s, 9, &r) && near(r->traffic_index, 0.25));   /* spaces around fields tolerated   */
    CHECK(!ts_find(&s, 2, NULL));    /* confidence 0.49 < 0.5 */
    CHECK(!ts_find(&s, 3, NULL));    /* not a number          */
    CHECK(!ts_find(&s, 4, NULL));    /* free-flow speed 0     */
    CHECK(!ts_find(&s, 5, NULL));    /* too few fields        */
    CHECK(!ts_find(&s, 6, NULL));    /* too many fields       */
    CHECK(!ts_find(&s, 7, NULL));    /* negative speed        */
    CHECK(!ts_find(&s, 8, NULL));    /* confidence above 1    */
    ts_free(&s);
}

static void test_missing_and_empty(void) {
    TrafficSnapshot s;
    CHECK(ts_load(&s, "tests/fixtures/does_not_exist.csv") == -1);
    CHECK(s.count == 0 && s.fetched_at[0] == '\0');
    CHECK(ts_find(&s, 1, NULL) == 0);                 /* usable after failure */
    CHECK(near(ts_traffic_index(&s, 1), 0.0));
    ts_free(&s);
    ts_free(&s);                                      /* double free is harmless */

    CHECK(ts_load(&s, "tests/fixtures/traffic_header_only.csv") == 0);
    CHECK(s.rows_read == 0 && s.count == 0 && s.fetched_at[0] == '\0');
    ts_free(&s);
}

/* Windows line endings, a missing final newline and an overlong line. */
static void test_line_endings_and_long_line(void) {
    const char *path = "tests/fixtures/.traffic_tmp.csv";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs("# fetched_at = 2026-01-02 03:04\r\n", f);
    fputs("edge_id,current_kph,freeflow_kph,confidence\r\n", f);
    fputs("10,20,40,0.9\r\n", f);
    fputs("junk,", f);
    for (int i = 0; i < 3000; i++) fputc('x', f);      /* one very long line */
    fputs("\r\n", f);
    fputs("11,30,40,0.8", f);                          /* no newline at the end */
    fclose(f);

    TrafficSnapshot s;
    CHECK(ts_load(&s, path) == 0);
    CHECK(strcmp(s.fetched_at, "2026-01-02 03:04") == 0);
    CHECK(s.rows_read == 3 && s.rows_skipped == 1 && s.count == 2);
    CHECK(near(ts_traffic_index(&s, 10), 0.5));
    CHECK(near(ts_traffic_index(&s, 11), 0.25));
    ts_free(&s);
    remove(path);
}

/* Scale: 50,000 rows, as many edges as the main roads could produce. */
static void test_many_rows(void) {
    const char *path = "tests/fixtures/.traffic_big_tmp.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs("edge_id,current_kph,freeflow_kph,confidence\n", f);
    for (int i = 0; i < 50000; i++) fprintf(f, "%d,%d,50,0.9\n", i * 3, i % 51);
    fclose(f);

    TrafficSnapshot s;
    CHECK(ts_load(&s, path) == 0);
    CHECK(s.count == 50000 && s.rows_skipped == 0);
    int ok = 1;
    for (int i = 0; i < 50000; i++) {
        double want = 1.0 - (double)(i % 51) / 50.0;
        if (want < 0.0) want = 0.0;
        if (!near(ts_traffic_index(&s, i * 3), want)) { ok = 0; break; }
    }
    CHECK(ok);
    CHECK(ts_find(&s, 1, NULL) == 0);                  /* ids not multiples of 3 are absent */
    ts_free(&s);
    remove(path);
}

int main(void) {
    test_valid();
    test_messy();
    test_missing_and_empty();
    test_line_endings_and_long_line();
    test_many_rows();
    TEST_REPORT("test_traffic_reader");
}
