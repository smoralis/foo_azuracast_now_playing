#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <windows.h>

namespace azuracast {

struct HttpResponse {
    bool success = false;
    int status_code = 0;
    std::vector<uint8_t> data;
    std::string error;

    std::string asText() const {
        return std::string(data.begin(), data.end());
    }
};

class HttpCancelToken {
public:
    void cancel();
    bool is_cancelled() const { return m_cancelled.load(); }

private:
    friend HttpResponse http_get(const std::wstring&, const std::shared_ptr<HttpCancelToken>&);
    void set_request(HINTERNET request);
    HINTERNET take_request();

    std::atomic<bool> m_cancelled{false};
    mutable std::mutex m_mutex;
    HINTERNET m_request = nullptr;
};

HttpResponse http_get(const std::wstring& url, const std::shared_ptr<HttpCancelToken>& token = {});

}
