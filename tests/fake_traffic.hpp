// fake_traffic.hpp — a TrafficProvider for tests: chosen edges get a measured
// speed, a congestion index and a confidence; every other edge has no data.
// Deriving from the abstract TrafficProvider is the point: the code under test
// does not know, or care, that these numbers are made up.
#pragma once
#include <map>
#include <string>
#include <utility>

#include "../src/oop/traffic_provider.hpp"

namespace rl_test {

class FakeTraffic : public rl::TrafficProvider {
public:
    void set(int edge, double kph, double index, double confidence = 0.9) {
        data_[edge] = Entry{ kph, index, confidence };
    }

    double trafficIndex(int edge) const override { return find(edge).index; }
    bool hasData(int edge) const override { return data_.count(edge) > 0; }
    double currentKph(int edge) const override { return find(edge).kph; }
    double confidence(int edge) const override { return find(edge).confidence; }
    int edgesWithData() const override { return static_cast<int>(data_.size()); }
    std::string description() const override { return "fake test data"; }

private:
    struct Entry { double kph = 0.0, index = 0.0, confidence = 0.0; };
    Entry find(int edge) const {
        auto it = data_.find(edge);
        return it == data_.end() ? Entry{} : it->second;
    }
    std::map<int, Entry> data_;
};

}  // namespace rl_test
