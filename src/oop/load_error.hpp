// load_error.hpp — thrown when a data file cannot be loaded.
#pragma once
#include <stdexcept>
#include <string>

namespace rl {

class LoadError : public std::runtime_error {
public:
    explicit LoadError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace rl
