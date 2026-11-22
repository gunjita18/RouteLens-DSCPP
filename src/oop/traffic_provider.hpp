// traffic_provider.hpp — where traffic data comes from, behind one interface.
//
//   TrafficProvider (abstract)
//     FreeFlowProvider   no traffic data: every edge is at free-flow speed
//     SnapshotProvider   a saved TomTom snapshot (data/traffic/traffic.csv), read
//                        by the C reader into a hash table
//
// The code that computes routes asks a TrafficProvider for numbers and does not
// know whether they come from a file or from nothing (abstraction); the choice is
// made once, at startup, by makeTrafficProvider().
//
// Edges are graph edge indices (OsmNetwork::graph->edges). The snapshot is keyed
// by edge_id = data row in edges.csv; SnapshotProvider translates through
// OsmNetwork::csvRow.
#pragma once
#include <memory>
#include <string>

#include "../core/traffic_reader.h"
#include "../data/osm_loader.hpp"
#include "../traffic/traffic.hpp"
#include "load_error.hpp"

namespace rl {

class TrafficProvider {
public:
    virtual ~TrafficProvider() = default;
    TrafficProvider(const TrafficProvider&) = delete;
    TrafficProvider& operator=(const TrafficProvider&) = delete;

    // 0 = free flow .. 1 = standstill; 0 for an edge without data.
    virtual double trafficIndex(int edge) const = 0;
    virtual bool hasData(int edge) const = 0;

    // Measured speed and its confidence; 0 for an edge without data.
    virtual double currentKph(int /*edge*/) const { return 0.0; }
    virtual double confidence(int /*edge*/) const { return 0.0; }

    // Number of graph edges that have data.
    virtual int edgesWithData() const { return 0; }

    // Shown to users, e.g. "estimated (free-flow speeds)" or "snapshot 2026-11-22 10:08".
    virtual std::string description() const = 0;
    // When the data was taken; empty if unknown or not applicable.
    virtual std::string fetchedAt() const { return std::string(); }

protected:
    TrafficProvider() = default;
};

class FreeFlowProvider : public TrafficProvider {
public:
    double trafficIndex(int) const override { return 0.0; }
    bool hasData(int) const override { return false; }
    std::string description() const override { return "estimated (free-flow speeds)"; }
};

class SnapshotProvider : public TrafficProvider {
public:
    // Reads csvPath. Throws LoadError if the file cannot be read. net must
    // outlive the provider.
    SnapshotProvider(const OsmNetwork& net, const std::string& csvPath);
    ~SnapshotProvider() override;

    double trafficIndex(int edge) const override;
    bool hasData(int edge) const override { return recordFor(edge) != nullptr; }
    double currentKph(int edge) const override;
    double confidence(int edge) const override;
    int edgesWithData() const override { return edgesWithData_; }
    std::string description() const override;
    std::string fetchedAt() const override { return snapshot_.fetched_at; }

    // Load report: data rows read and rejected by the C reader, and rows whose
    // edge is not in the cleaned network.
    int rowsRead() const { return snapshot_.rows_read; }
    int rowsSkipped() const { return snapshot_.rows_skipped; }
    int unmatchedRows() const { return snapshot_.count - edgesWithData_; }

private:
    const TrafficRecord* recordFor(int edge) const;   // nullptr if the edge has no data

    const OsmNetwork& net_;
    TrafficSnapshot snapshot_;    // owned C structure (hash table inside), freed in the destructor
    int edgesWithData_ = 0;
};

// Snapshot if csvPath can be read, otherwise FreeFlowProvider. If the file is
// missing or unreadable, *warning (optional) says why; the demo still works, with
// speeds labelled "estimated".
std::unique_ptr<TrafficProvider> makeTrafficProvider(const OsmNetwork& net, const std::string& csvPath,
                                                     std::string* warning = nullptr);

// Writes the provider's data into state (only edges that have data; the rest stay
// as they are). Travel time on such an edge is length / measured speed; a measured
// speed of 0 marks the edge closed and keeps its free-flow time. The edges are
// marked DataSource::Historical, because a saved snapshot is not live traffic.
// Returns the number of edges updated.
int applyProvider(TrafficState& state, const TrafficProvider& provider);

}  // namespace rl
