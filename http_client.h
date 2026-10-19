#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include <windows.h>

namespace azuracast {

    struct HttpResponse {
        bool success = false;
        int status_code = 0;
        std::vector<uint8_t> data;
        std::string error;

        std::string content_type;   // lowercased, e.g. "audio/mpeg"
        bool not_json = false;      // set when require_json was requested and the response isn't JSON

        std::string asText() const {
            return std::string(data.begin(), data.end());
        }
    };

    class HttpCancelToken {
    public:
        void cancel();
        bool is_cancelled() const { return m_cancelled.load(); }

    private:
        // Must match the definition's signature exactly; friend declarations can't carry default arguments.
        friend HttpResponse http_get(const std::wstring&, const std::shared_ptr<HttpCancelToken>&,
            size_t, bool);
        void set_request(HINTERNET request);
        HINTERNET take_request();

        std::atomic<bool> m_cancelled{ false };
        mutable std::mutex m_mutex;
        HINTERNET m_request = nullptr;
    };

    HttpResponse http_get(const std::wstring& url,
        const std::shared_ptr<HttpCancelToken>& token = {},
        size_t max_bytes = 0,        // 0 = unlimited
        bool require_json = false);  // true: abort early unless Content-Type contains "json"

}