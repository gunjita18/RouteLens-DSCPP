// http_client.hpp — minimal libcurl wrapper (C++).
#pragma once
#include <string>
#include <vector>

namespace rl {

struct HttpResponse {
    long status = 0;          // HTTP status code, 0 if no response at all
    std::string body;
    std::string error;        // transport-level error (DNS, timeout, TLS...)
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    // Human-readable one-liner for logs and on-screen messages.
    std::string describe() const;
};

// One instance per program; constructs/destroys curl's global state.
class HttpClient {
public:
    HttpClient(std::string userAgent, long timeoutSeconds = 45);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const std::string& url, const std::vector<std::string>& headers = {}) const;
    HttpResponse post(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers) const;

private:
    HttpResponse perform(const std::string& url, const std::string* body,
                         const std::vector<std::string>& headers) const;
    std::string userAgent_;
    long timeout_;
};

// RFC 3986 percent-encoding (unreserved chars kept). Independent of curl.
std::string urlEncode(const std::string& s);

}  // namespace rl
