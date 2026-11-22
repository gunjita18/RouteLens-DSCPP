# Traffic snapshot

`traffic.csv` goes in this folder. It is written once before the demo by
`preprocess/fetch_traffic.py` (map-data and traffic part, Phase III design,
section 6), so the demo works without internet access:

```
edge_id,current_kph,freeflow_kph,confidence
1452,33,42,0.60
# fetched_at = 2026-11-10 09:30
```

- The server loads it at startup with `SnapshotProvider` (C reader inside).
  Routes then use the measured speeds (Normal and Incident scenarios) and the
  website shows "Traffic data: 10 Nov, 9:30 AM". Without the file, and for
  Rain and Rush hour, it shows "Traffic data: estimated (free-flow speeds)".
- Another location can be given with `ROUTELENS_TRAFFIC_FILE`.
- Docker: the snapshot is not part of the image; mount this folder
  (see the Dockerfile), as the traffic module decided.
- The file holds no secret (the TomTom key is only used by the Python script).
