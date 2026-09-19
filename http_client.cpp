#include "stdafx.h"
#include "http_client.h"
#include <atomic>

namespace azuracast {

void HttpCancelToken::set_request(HINTERNET request) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cancelled.load()) {
        if (request) WinHttpCloseHandle(request);
        return;
    }
    m_request = request;
}

HINTERNET HttpCancelToken::take_request() {
    std::lock_guard<std::mutex> lock(m_mutex);
    HINTERNET request = m_request;
    m_request = nullptr;
    return request;
}

void HttpCancelToken::cancel() {
    HINTERNET request = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelled = true;
        request = m_request;
        m_request = nullptr;
    }
    if (request) WinHttpCloseHandle(request);
}

HttpResponse http_get(const std::wstring& url, const std::shared_ptr<HttpCancelToken>& token) {
    HttpResponse result;
    auto cancelled = [&]() { return token && token->is_cancelled(); };

    if (cancelled()) {
        result.error = "request cancelled";
        return result;
    }

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = _countof(path);

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) {
        result.error = "failed to parse URL";
        return result;
    }

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"foobar2000-foo_azuracast_now_playing/1.0",
                                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { result.error = "WinHttpOpen failed"; return result; }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        result.error = cancelled() ? "request cancelled" : "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        result.error = cancelled() ? "request cancelled" : "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (token) token->set_request(hRequest);

    BOOL sent = FALSE;
    if (!cancelled()) {
        sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }
    if (sent && !cancelled()) sent = WinHttpReceiveResponse(hRequest, nullptr);

    if (sent && !cancelled()) {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        result.status_code = (int)statusCode;

        DWORD bytesAvailable = 0;
        while (!cancelled() && WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<uint8_t> chunk(bytesAvailable);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead)) break;
            if (cancelled()) break;
            chunk.resize(bytesRead);
            result.data.insert(result.data.end(), chunk.begin(), chunk.end());
        }
        if (cancelled()) {
            result.data.clear();
            result.error = "request cancelled";
        } else {
            result.success = (statusCode >= 200 && statusCode < 300);
            if (!result.success) result.error = "HTTP status " + std::to_string(statusCode);
        }
    } else {
        result.error = cancelled() ? "request cancelled" :
                       "request failed, GetLastError=" + std::to_string(GetLastError());
    }

    HINTERNET requestToClose = token ? token->take_request() : hRequest;
    if (requestToClose) WinHttpCloseHandle(requestToClose);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

}
