#ifndef ROUTELENS_PLACE_INDEX_HPP
#define ROUTELENS_PLACE_INDEX_HPP

#include <new>
#include <string>
#include <vector>

#include "../core/trie.h"
#include "../data/osm_loader.hpp"

namespace rl {

struct Place {
    std::string name;
    LatLon pos;
};

// Place-name search box: a C++ wrapper around the C trie (src/core/trie.c).
//
// The class owns the C structure and frees it in its destructor (RAII), so
// the trie cannot leak or be freed twice; copying is disabled for the same
// reason. The trie stores, for each name, its index in places_.
//
// Filled once at startup, then only read: search() is const and safe to call
// from several request threads at the same time.
class PlaceIndex {
public:
    PlaceIndex() {
        if (!trie_init(&trie_)) {
            throw std::bad_alloc();
        }
    }
    ~PlaceIndex() { trie_free(&trie_); }

    PlaceIndex(const PlaceIndex&) = delete;
    PlaceIndex& operator=(const PlaceIndex&) = delete;

    // Returns false if the name cannot be stored (empty or non-ASCII).
    bool add(const std::string& name, LatLon pos) {
        const int index = static_cast<int>(places_.size());
        if (!trie_insert(&trie_, name.c_str(), index)) {
            return false;
        }
        places_.push_back({name, pos});
        return true;
    }

    // Up to `max` places whose name starts with `prefix` (any letter case),
    // in alphabetical order.
    std::vector<Place> search(const std::string& prefix, int max) const {
        std::vector<int> found(max > 0 ? static_cast<std::size_t>(max) : 0);
        const int n = trie_prefix(&trie_, prefix.c_str(), found.data(), max);
        std::vector<Place> out;
        for (int i = 0; i < n; ++i) {
            out.push_back(places_[static_cast<std::size_t>(found[i])]);
        }
        return out;
    }

    std::size_t size() const { return places_.size(); }

private:
    Trie trie_{};
    std::vector<Place> places_;
};

}  // namespace rl

#endif
