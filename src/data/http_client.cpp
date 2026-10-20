#include "http_client.hpp"

#include <curl/curl.h>

#include <cstdio>

namespace rl {

std::string HttpResponse::describe() const {
    if (!error.empty()) return "network error: " + error;
    std::string s = "HTTP " + std::to_string(status);
    if (status == 401 || status == 403) s += " (check your API key / User-Agent)";
    else if (status == 429) s += " (rate limited)";
    else if (status >= 500) s += " (server error, try again later)";
    return s;
}

// libcurl calls this for each received chunk; we append it to a std::string.
static size_t writeCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;   // returning anything else tells curl to abort
}

HttpClient::HttpClient(std::string userAgent, long timeoutSeconds)
    : userAgent_(std::move(userAgent)), timeout_(timeoutSeconds) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient() { curl_global_cleanup(); }

HttpResponse HttpClient::get(const std::string& url, const std::vector<std::string>& headers) const {
    return perform(url, nullptr, headers);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body,
                              const std::vector<std::string>& headers) const {
    return perform(url, &body, headers);
}

HttpResponse HttpClient::perform(const std::string& url, const std::string* body,
                                 const std::vector<std::string>& headers) const {
    HttpResponse r;
    CURL* curl = curl_easy_init();
    if (!curl) { r.error = "curl_easy_init failed"; return r; }

    curl_slist* headerList = nullptr;
    for (const auto& h : headers) headerList = curl_slist_append(headerList, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   // accept gzip/deflate
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    if (headerList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());   // not copied: body outlives perform
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body->size());
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) r.error = curl_easy_strerror(rc);
    else curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return r;
}

std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

}  // namespace rl
