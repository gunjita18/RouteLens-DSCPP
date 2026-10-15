#include "api_clients.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>

namespace rl {

void Throttle::wait() {
    auto now = std::chrono::steady_clock::now();
    if (!first_) {
        auto next = last_ + interval_;
        if (now < next) std::this_thread::sleep_for(next - now);
    }
    first_ = false;
    last_ = std::chrono::steady_clock::now();
}

static std::string coord(LatLon p) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "[%.6f,%.6f]", p.lon, p.lat);   // ORS wants [lon, lat]
    return buf;
}

// ------------------------------------------------------------------ ORS

OrsClient::OrsClient(const HttpClient& http, const Config& cfg)
    : http_(http), cfg_(cfg), throttle_(cfg.orsMinIntervalMs) {}

HttpResponse OrsClient::postWithRetry(const std::string& url, const std::string& body) {
    const std::vector<std::string> headers = {
        "Authorization: " + cfg_.orsApiKey,
        "Content-Type: application/json; charset=utf-8",
        "Accept: application/json, application/geo+json",
    };
    HttpResponse r;
    for (int attempt = 1; attempt <= 3; attempt++) {
        throttle_.wait();
        r = http_.post(url, body, headers);
        bool transient = !r.error.empty() || r.status == 429 || r.status >= 500;
        if (r.ok() || !transient || attempt == 3) break;
        int waitS = r.status == 429 ? 61 : 5 * attempt;   // 429: wait out the per-minute window
        std::cerr << "  ORS " << r.describe() << " — retrying in " << waitS << " s (attempt "
                  << attempt << "/3)\n";
        std::this_thread::sleep_for(std::chrono::seconds(waitS));
    }
    return r;
}

HttpResponse OrsClient::matrix(const std::vector<LatLon>& locations) {
    std::ostringstream body;
    body << "{\"locations\":[";
    for (size_t i = 0; i < locations.size(); i++) body << (i ? "," : "") << coord(locations[i]);
    body << "],\"metrics\":[\"distance\",\"duration\"],\"units\":\"m\"}";
    return postWithRetry(cfg_.orsBaseUrl + "/v2/matrix/" + cfg_.orsProfile, body.str());
}

HttpResponse OrsClient::directions(LatLon from, LatLon to) {
    std::string body = "{\"coordinates\":[" + coord(from) + "," + coord(to) +
                       "],\"extra_info\":[\"waytype\"],\"instructions\":false}";
    return postWithRetry(cfg_.orsBaseUrl + "/v2/directions/" + cfg_.orsProfile + "/geojson", body);
}

// ------------------------------------------------------------ Nominatim

NominatimClient::NominatimClient(const HttpClient& http, const Config& cfg) : http_(http), cfg_(cfg) {}

HttpResponse NominatimClient::search(const std::string& query) {
    std::string url = cfg_.nominatimUrl + "/search?format=jsonv2&limit=1&q=" + urlEncode(query);
    if (!cfg_.countryCodes.empty()) url += "&countrycodes=" + urlEncode(cfg_.countryCodes);
    if (!cfg_.nominatimViewbox.empty()) url += "&viewbox=" + urlEncode(cfg_.nominatimViewbox) + "&bounded=1";
    throttle_.wait();
    return http_.get(url);
}

// ------------------------------------------------------------- Overpass

OverpassClient::OverpassClient(const HttpClient& http, const Config& cfg) : http_(http), cfg_(cfg) {}

std::string OverpassClient::buildQuery(const BBox& b) {
    char buf[256];
    // Overpass bbox order is (south, west, north, east).
    std::snprintf(buf, sizeof buf,
                  "[out:json][timeout:60];node[\"highway\"=\"traffic_signals\"](%.6f,%.6f,%.6f,%.6f);out;",
                  b.south, b.west, b.north, b.east);
    return buf;
}

HttpResponse OverpassClient::trafficSignals(const BBox& box) {
    std::string body = "data=" + urlEncode(buildQuery(box));
    HttpResponse r;
    for (int attempt = 1; attempt <= 3; attempt++) {
        r = http_.post(cfg_.overpassUrl, body, { "Content-Type: application/x-www-form-urlencoded" });
        bool transient = !r.error.empty() || r.status == 429 || r.status == 504 || r.status >= 500;
        if (r.ok() || !transient || attempt == 3) break;
        std::cerr << "  Overpass " << r.describe() << " — retrying in " << 10 * attempt << " s\n";
        std::this_thread::sleep_for(std::chrono::seconds(10 * attempt));
    }
    return r;
}

// ---------------------------------------------------------------- Tiles

TileClient::TileClient(const HttpClient& http, const Config& cfg) : http_(http), cfg_(cfg) {}

HttpResponse TileClient::tile(int z, int x, int y) {
    throttle_.wait();
    return http_.get(cfg_.tileUrl + "/" + std::to_string(z) + "/" + std::to_string(x) + "/" +
                     std::to_string(y) + ".png");
}

}  // namespace rl
