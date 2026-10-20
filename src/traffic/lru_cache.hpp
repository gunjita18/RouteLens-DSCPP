#ifndef ROUTELENS_LRU_CACHE_HPP
#define ROUTELENS_LRU_CACHE_HPP

#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace rl {

template <typename Key, typename Value>
class LRUCache {
private:
    using Entry = std::pair<Key, Value>;
    using Iterator = typename std::list<Entry>::iterator;

    std::size_t capacity_;
    std::list<Entry> entries_;
    std::unordered_map<Key, Iterator> index_;

public:
    explicit LRUCache(std::size_t capacity)
        : capacity_(capacity) {}

    std::size_t size() const {
        return entries_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return entries_.empty();
    }

    bool contains(const Key& key) const {
        return index_.find(key) != index_.end();
    }

    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }

        entries_.splice(entries_.begin(), entries_, it->second);
        return it->second->second;
    }

    void put(const Key& key, const Value& value) {
        if (capacity_ == 0) {
            return;
        }

        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = value;
            entries_.splice(entries_.begin(), entries_, it->second);
            return;
        }

        if (entries_.size() >= capacity_) {
            index_.erase(entries_.back().first);
            entries_.pop_back();
        }

        entries_.emplace_front(key, value);
        index_[key] = entries_.begin();
    }

    bool erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }

        entries_.erase(it->second);
        index_.erase(it);
        return true;
    }

    void clear() {
        entries_.clear();
        index_.clear();
    }
};

}  // namespace rl

#endif
