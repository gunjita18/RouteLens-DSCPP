// json_util.hpp — thin RAII + accessor helpers over cJSON.
#pragma once
#include <cjson/cJSON.h>

#include <string>

namespace rl {

// Owns a parsed cJSON tree; frees it on scope exit (no leaks on early return).
class JsonDoc {
public:
    explicit JsonDoc(const std::string& text) : root_(cJSON_Parse(text.c_str())) {}
    explicit JsonDoc(cJSON* adopt) : root_(adopt) {}
    ~JsonDoc() { cJSON_Delete(root_); }
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
    cJSON* get() const { return root_; }
    explicit operator bool() const { return root_ != nullptr; }

private:
    cJSON* root_;
};

inline const cJSON* jget(const cJSON* obj, const char* key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

inline bool jnum(const cJSON* obj, const char* key, double& out) {
    const cJSON* it = jget(obj, key);
    if (!cJSON_IsNumber(it)) return false;
    out = it->valuedouble;
    return true;
}

inline bool jstr(const cJSON* obj, const char* key, std::string& out) {
    const cJSON* it = jget(obj, key);
    if (!cJSON_IsString(it) || !it->valuestring) return false;
    out = it->valuestring;
    return true;
}

// Serialises and frees the cJSON-allocated buffer.
inline std::string jprint(const cJSON* node, bool pretty) {
    char* s = pretty ? cJSON_Print(node) : cJSON_PrintUnformatted(node);
    if (!s) return {};
    std::string out(s);
    cJSON_free(s);
    return out;
}

}  // namespace rl
