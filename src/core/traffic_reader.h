/*
 * traffic_reader.h - reads the traffic snapshot (traffic.csv) into a hash table.
 *
 * traffic.csv is written once, before the demo, by preprocess/fetch_traffic.py:
 *
 *     # fetched_at = 2026-11-10 09:30
 *     edge_id,current_kph,freeflow_kph,confidence
 *     1452,33,42,0.60
 *
 * edge_id is the 0-based data row of the edge in data/osm/edges.csv. The C code
 * never touches the network: if the file is missing, every edge simply has
 * traffic index 0 (free-flow speed) and the caller labels the data "estimated".
 *
 * For each row:  traffic_index = 1 - current / freeflow, clamped to [0, 1].
 * Rows are rejected (counted in rows_skipped) when they are malformed, have a
 * duplicate edge_id, or have confidence below TS_MIN_CONFIDENCE.
 */
#ifndef ROUTELENS_TRAFFIC_READER_H
#define ROUTELENS_TRAFFIC_READER_H

#include "hashtable.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS_MIN_CONFIDENCE 0.5      /* the single confidence rule: ignore below this */
#define TS_FETCHED_AT_LEN 64

typedef struct {
    long long edge_id;
    double current_kph;
    double freeflow_kph;
    double confidence;
    double traffic_index;          /* 0 = free flow .. 1 = standstill */
} TrafficRecord;

typedef struct {
    TrafficRecord *records;        /* in file order */
    int count;
    int cap;
    HashTable index;               /* edge_id -> position in records */
    int rows_read;                 /* data rows seen (comments, header, blanks excluded) */
    int rows_skipped;              /* of those, rejected */
    char fetched_at[TS_FETCHED_AT_LEN];   /* "" if the file has no fetched_at line */
} TrafficSnapshot;

/* Loads path. Returns 0 on success (even if some rows were skipped), -1 if the
 * file cannot be opened or memory runs out. After -1 the snapshot is empty
 * and valid: ts_find fails and ts_traffic_index returns 0. Always call ts_free. */
int  ts_load(TrafficSnapshot *s, const char *path);
void ts_free(TrafficSnapshot *s);

/* Returns 1 and sets *out (may be NULL) if the edge has data, else 0. */
int  ts_find(const TrafficSnapshot *s, long long edge_id, const TrafficRecord **out);

/* Traffic index of the edge, or 0.0 (free flow) if the edge has no data. */
double ts_traffic_index(const TrafficSnapshot *s, long long edge_id);

#ifdef __cplusplus
}
#endif
#endif
