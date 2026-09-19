#include "stdafx.h"
#include "now_playing_state.h"
#include "http_client.h"
#include "json_lite.h"
#include "settings.h"
#include <foobar2000/SDK/foobar2000.h>
#include <shlwapi.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

#define PROBE_LOG azuracast::probe_logger()

namespace azuracast {

    namespace {
        struct probe_logger {
            pfc::string8 buffer;
            probe_logger() { buffer << "foo_azuracast_now_playing:  "; }
            ~probe_logger() { console::print(buffer); }
            template <typename T>
            probe_logger& operator<<(const T& value) { buffer << value; return *this; }
        };

        constexpr double kMaxCoverSyncSeconds = 600.0;

        unsigned parse_coversync_ms(const std::wstring& url) {
            const size_t q = url.find(L'?');
            if (q == std::wstring::npos) return 0;

            const size_t hash = url.find(L'#', q);
            const std::wstring query = url.substr(
                q + 1, hash == std::wstring::npos ? std::wstring::npos : hash - q - 1);

            size_t pos = 0;
            while (pos <= query.size()) {
                const size_t amp = query.find(L'&', pos);
                const std::wstring pair = query.substr(
                    pos, amp == std::wstring::npos ? std::wstring::npos : amp - pos);

                const size_t eq = pair.find(L'=');
                if (eq != std::wstring::npos &&
                    _wcsicmp(pair.substr(0, eq).c_str(), L"coversync") == 0) {
                    const std::wstring value = pair.substr(eq + 1);
                    wchar_t* end = nullptr;
                    double secs = wcstod(value.c_str(), &end);
                    if (end != value.c_str() && *end == L'\0' && secs > 0.0) {
                        secs = std::min(secs, kMaxCoverSyncSeconds);
                        return (unsigned)(secs * 1000.0);
                    }
                    return 0;
                }

                if (amp == std::wstring::npos) break;
                pos = amp + 1;
            }
            return 0;
        }

        class playback_trigger : public play_callback_static {
        public:
            unsigned get_flags() override {
                return flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop;
            }

            void on_playback_starting(playback_control::t_track_command, bool paused) override {
                if (paused) return;

                const unsigned interval = (unsigned)std::max((int)cfg_poll_interval, 1000);
                if (!cfg_station_url.get().is_empty()) {
                    shared_service().start(
                        pfc::stringcvt::string_wide_from_utf8(cfg_station_url.get_ptr()).get_ptr(),
                        interval
                    );
                }
            }

            void on_playback_stop(playback_control::t_stop_reason) override {
                shared_service().stop();
            }

            void on_playback_new_track(metadb_handle_ptr track) override {
                if (cfg_station_url.get().is_empty() && track.is_valid()) {
                    const unsigned interval = (unsigned)std::max((int)cfg_poll_interval, 1000);
                    std::wstring track_path = pfc::stringcvt::string_wide_from_utf8(track->get_path()).get_ptr();
                    shared_service().start_auto(track_path, interval);
                }
            }

            void on_playback_seek(double) override {}
            void on_playback_pause(bool) override {}
            void on_playback_edited(metadb_handle_ptr) override {}
            void on_playback_dynamic_info(const file_info&) override {}
            void on_playback_dynamic_info_track(const file_info&) override {}
            void on_playback_time(double) override {}
            void on_volume_change(float) override {}
        };

        FB2K_SERVICE_FACTORY(playback_trigger);
    }

    NowPlayingService& shared_service() {
        static NowPlayingService instance;
        return instance;
    }

    NowPlayingService::NowPlayingService() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr);
    }

    NowPlayingService::~NowPlayingService() {
        stop();
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_art_bitmap) DeleteObject(m_art_bitmap);
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }

    NowPlayingService::UpdateToken NowPlayingService::add_on_update(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const UpdateToken token = m_next_update_token++;
        m_on_update[token] = std::move(cb);
        return token;
    }

    void NowPlayingService::remove_on_update(UpdateToken token) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_on_update.erase(token);
    }

    void NowPlayingService::notify_update() {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            callbacks.reserve(m_on_update.size());
            for (auto& entry : m_on_update) {
                callbacks.push_back(entry.second);
            }
        }
        for (auto& cb : callbacks) {
            if (cb) cb();
        }
    }

    void NowPlayingService::start(const std::wstring& station_url, unsigned poll_interval_ms) {
        stop();
        m_stop = false;
        m_sync_delay_ms = parse_coversync_ms(station_url);
        if (m_sync_delay_ms) PROBE_LOG << "cover sync delay: " << (t_size)m_sync_delay_ms << " ms";
        auto cancel = std::make_shared<HttpCancelToken>();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_http_cancel = cancel;
        }
        m_thread = std::thread(&NowPlayingService::worker_thread, this, station_url, poll_interval_ms, cancel);
    }

    void NowPlayingService::start_auto(const std::wstring& stream_url, unsigned poll_interval_ms) {
        if (stream_url.empty()) {
            stop();
            return;
        }

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);

        if (!WinHttpCrackUrl(
            stream_url.c_str(),
            (DWORD)stream_url.size(),
            0,
            &uc) ||
            (uc.nScheme != INTERNET_SCHEME_HTTP &&
                uc.nScheme != INTERNET_SCHEME_HTTPS)) {

            stop();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot = NowPlayingSnapshot{};
                m_snapshot.error = "This is not a stream";
            }

            notify_update();
            return;
        }

        stop();
        m_stop = false;
        m_sync_delay_ms = parse_coversync_ms(stream_url);
        if (m_sync_delay_ms) PROBE_LOG << "cover sync delay: " << (t_size)m_sync_delay_ms << " ms";

        auto cancel = std::make_shared<HttpCancelToken>();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_http_cancel = cancel;
        }

        m_thread = std::thread(
            &NowPlayingService::auto_worker_thread,
            this,
            stream_url,
            poll_interval_ms,
            cancel
        );
    }

    void NowPlayingService::stop() {
        m_stop = true;

        std::shared_ptr<HttpCancelToken> cancel;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cancel = m_http_cancel;
            m_http_cancel.reset();
        }

        if (cancel) cancel->cancel();
        if (m_thread.joinable()) m_thread.join();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snapshot = NowPlayingSnapshot{};
            m_current_art_url.clear();
            m_last_fetched_art_url.clear();
            m_last_countdown_s = -1;
            clear_pending_locked();

            if (m_art_bitmap) {
                DeleteObject(m_art_bitmap);
                m_art_bitmap = nullptr;
            }
        }

        notify_update();
    }

    void NowPlayingService::refresh_from_preferences() {
        if (cfg_station_url.get().is_empty()) {
            stop();
            return;
        }

        start(
            pfc::stringcvt::string_wide_from_utf8(cfg_station_url.get_ptr()).get_ptr(),
            (unsigned)std::max((int)cfg_poll_interval, 1000)
        );
    }

    void NowPlayingService::auto_worker_thread(std::wstring stream_url, unsigned poll_interval_ms, std::shared_ptr<HttpCancelToken> cancel) {
        std::wstring endpoint;

        while (!m_stop) {
            if (endpoint.empty()) {
                endpoint = discover_station_endpoint(stream_url, cancel);
                if (endpoint.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_snapshot = NowPlayingSnapshot{};
                        m_snapshot.error = cancel && cancel->is_cancelled()
                            ? "request cancelled"
                            : "AzuraCast API endpoint not found for this stream";
                    }
                    notify_update();

                    break;
                }
            }

            if (!endpoint.empty()) {
                poll_once(endpoint, cancel);
            }

            for (unsigned waited = 0; waited < poll_interval_ms && !m_stop; waited += 200) {
                release_due();
                Sleep(200);
            }
        }
    }

    std::wstring NowPlayingService::discover_station_endpoint(const std::wstring& stream_url, const std::shared_ptr<HttpCancelToken>& cancel) {
        if (stream_url.empty() || (cancel && cancel->is_cancelled())) {
            PROBE_LOG << "probe skipped: empty stream URL or cancelled";
            return {};
        }

        PROBE_LOG << "starting probe for stream URL: " << pfc::stringcvt::string_utf8_from_wide(stream_url.c_str());

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[512] = {};
        wchar_t path[4096] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = _countof(host);
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = _countof(path);
        if (!WinHttpCrackUrl(stream_url.c_str(), (DWORD)stream_url.size(), 0, &uc)) {
            PROBE_LOG << "WinHttpCrackUrl failed";
            return {};
        }

        const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
        std::wstring scheme = https ? L"https://" : L"http://";
        std::wstring base = scheme + host;
        PROBE_LOG << "probe base: " << pfc::stringcvt::string_utf8_from_wide(base.c_str());

        std::wstring p(path);
        static const wchar_t* const markers[] = { L"/listen/", L"/hls/" };
        for (const wchar_t* marker_ptr : markers) {
            const std::wstring marker = marker_ptr;
            size_t pos = p.find(marker);
            if (pos == std::wstring::npos) continue;

            size_t start = pos + marker.size();
            size_t end = p.find(L'/', start);
            if (end == std::wstring::npos) end = p.size();
            if (end <= start) continue;

            std::wstring shortcode = p.substr(start, end - start);
            std::wstring candidate = base + L"/api/nowplaying/" + shortcode;
            PROBE_LOG << "trying station endpoint (" << pfc::stringcvt::string_utf8_from_wide(marker.c_str())
                << "): " << pfc::stringcvt::string_utf8_from_wide(candidate.c_str());
            HttpResponse r = http_get(candidate, cancel);
            PROBE_LOG << "station endpoint result: success=" << (r.success ? 1 : 0)
                << " status=" << r.status_code << " error=" << r.error.c_str();
            if (r.success) {
                try {
                    Json root = Json::parse(r.asText());
                    if (root["station"]["shortcode"].asString() ==
                        pfc::stringcvt::string_utf8_from_wide(shortcode.c_str()).get_ptr()) {
                        PROBE_LOG << "station endpoint matched shortcode: "
                            << pfc::stringcvt::string_utf8_from_wide(shortcode.c_str());
                        return candidate;
                    }
                    if (!root["station"].isNull() && !root["station"]["shortcode"].asString().empty()) {
                        PROBE_LOG << "station endpoint returned a valid station: " << root["station"]["shortcode"].asString().c_str();
                        return candidate;
                    }
                    PROBE_LOG << "station endpoint returned JSON, but no usable station data";
                }
                catch (...) {
                }
            }
        }

        const std::wstring all_endpoint = base + L"/api/nowplaying";
        PROBE_LOG << "trying all-stations endpoint: " << pfc::stringcvt::string_utf8_from_wide(all_endpoint.c_str());
        HttpResponse all = http_get(all_endpoint, cancel);
        PROBE_LOG << "all-stations result: success=" << (all.success ? 1 : 0)
            << " status=" << all.status_code << " error=" << all.error.c_str();
        if (!all.success) return {};

        try {
            Json root = Json::parse(all.asText());
            if (root.type() != Json::Type::Array) {
                PROBE_LOG << "all-stations response was not an array";
                return {};
            }
            PROBE_LOG << "checking " << (t_size)root.size() << " station(s) for a stream URL match";

            std::string wanted = pfc::stringcvt::string_utf8_from_wide(stream_url.c_str()).get_ptr();
            auto normalize = [](std::string v) {
                size_t q = v.find('?');
                if (q != std::string::npos) v.resize(q);
                while (v.size() > 1 && v.back() == '/') v.pop_back();
                return v;
                };
            wanted = normalize(wanted);

            for (size_t i = 0; i < root.size(); ++i) {
                const Json& station = root[i]["station"];
                const std::string listen = normalize(station["listen_url"].asString());
                if (!listen.empty() && listen == wanted) {
                    const std::string shortcode = station["shortcode"].asString();
                    if (!shortcode.empty()) {
                        PROBE_LOG << "matched station.listen_url -> shortcode=" << shortcode.c_str();
                        return base + L"/api/nowplaying/" +
                            pfc::stringcvt::string_wide_from_utf8(shortcode.c_str()).get_ptr();
                    }
                }

                const Json& mounts = station["mounts"];
                for (size_t m = 0; m < mounts.size(); ++m) {
                    if (normalize(mounts[m]["url"].asString()) == wanted) {
                        const std::string shortcode = station["shortcode"].asString();
                        if (!shortcode.empty()) {
                            PROBE_LOG << "matched mount URL -> shortcode=" << shortcode.c_str();
                            return base + L"/api/nowplaying/" +
                                pfc::stringcvt::string_wide_from_utf8(shortcode.c_str()).get_ptr();
                        }
                    }
                }
            }
        }
        catch (...) {
            PROBE_LOG << "exception while parsing/matching all-stations response";
        }

        PROBE_LOG << "probe failed: no matching AzuraCast station endpoint found";
        return {};
    }

    void NowPlayingService::worker_thread(std::wstring station_url, unsigned poll_interval_ms, std::shared_ptr<HttpCancelToken> cancel) {
        while (!m_stop) {
            poll_once(station_url, cancel);
            for (unsigned waited = 0; waited < poll_interval_ms && !m_stop; waited += 200) {
                release_due();
                Sleep(200);
            }
        }
    }

    void NowPlayingService::poll_once(const std::wstring& station_url, const std::shared_ptr<HttpCancelToken>& cancel) {
        HttpResponse resp = http_get(station_url, cancel);
        const auto fetched_at = std::chrono::steady_clock::now();

        NowPlayingSnapshot snap;
        if (!resp.success) {
            snap.valid = false;
            snap.error = resp.error.empty() ? "request failed" : resp.error;
        }
        else {
            try {
                Json root = Json::parse(resp.asText());
                const Json& station = root["station"];
                const Json& np = root["now_playing"];
                const Json& song = np["song"];

                snap.valid = true;
                snap.station.name = station["name"].asString("Unknown Station");
                snap.station.description = station["description"].asString("");
                snap.station.next = root["playing_next"]["song"]["text"].asString("");
                snap.song.title = song["title"].asString("Unknown Title");
                snap.song.artist = song["artist"].asString("Unknown Artist");
                snap.song.album = song["album"].asString("");
                snap.song.art_url = song["art"].asString("");
                snap.listeners = root["listeners"]["current"].asInt(-1);
                snap.elapsed = np["elapsed"].asDouble(-1);
                snap.duration = np["duration"].asDouble(-1);
            }
            catch (const std::exception& e) {
                snap.valid = false;
                snap.error = std::string("JSON parse error: ") + e.what();
            }
        }

        const unsigned delay_ms = m_sync_delay_ms.load();
        if (delay_ms > 0) {
            bool need_art = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                need_art = snap.valid && !snap.song.art_url.empty() &&
                    snap.song.art_url != m_last_fetched_art_url;
            }

            HBITMAP art = nullptr;
            std::string art_url;
            if (need_art) {
                art = fetch_art_bitmap(snap.song.art_url, cancel);
                if (art) art_url = snap.song.art_url;
            }

            if (cancel && cancel->is_cancelled()) {
                if (art) DeleteObject(art);
                return;
            }

            bool shown_now = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!art_url.empty()) m_last_fetched_art_url = art_url;

                if (!m_snapshot.valid && !snap.valid && m_pending.empty()) {
                    m_snapshot = std::move(snap);
                    shown_now = true;
                }
                else {
                    PendingUpdate pending;
                    pending.ready_at = fetched_at + std::chrono::milliseconds(delay_ms);
                    pending.snap = std::move(snap);
                    pending.art = art;
                    pending.art_url = art_url;
                    m_pending.push_back(std::move(pending));
                }
            }

            if (shown_now) notify_update();
            return;
        }

        bool art_changed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            art_changed = snap.valid && !snap.song.art_url.empty() && snap.song.art_url != m_current_art_url;
            m_snapshot = snap;
        }

        if (art_changed) {
            update_art(snap.song.art_url, cancel);
        }

        notify_update();
    }

    HBITMAP NowPlayingService::fetch_art_bitmap(const std::string& art_url, const std::shared_ptr<HttpCancelToken>& cancel) {
        if (art_url.empty()) return nullptr;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, art_url.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return nullptr;
        std::wstring wurl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, art_url.c_str(), -1, &wurl[0], wlen);
        if (!wurl.empty() && wurl.back() == 0) wurl.pop_back();

        HttpResponse resp = http_get(wurl, cancel);
        if (!resp.success || resp.data.empty()) return nullptr;

        IStream* stream = SHCreateMemStream(resp.data.data(), (UINT)resp.data.size());
        if (!stream) return nullptr;

        Gdiplus::Bitmap bitmap(stream, FALSE);
        stream->Release();

        if (bitmap.GetLastStatus() != Gdiplus::Ok) return nullptr;

        HBITMAP hbmp = nullptr;
        bitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &hbmp);
        if (!hbmp) return nullptr;

        if (cancel && cancel->is_cancelled()) {
            DeleteObject(hbmp);
            return nullptr;
        }

        return hbmp;
    }

    void NowPlayingService::update_art(const std::string& art_url, const std::shared_ptr<HttpCancelToken>& cancel) {
        HBITMAP hbmp = fetch_art_bitmap(art_url, cancel);
        if (!hbmp) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (cancel && cancel->is_cancelled()) {
            DeleteObject(hbmp);
            return;
        }
        if (m_art_bitmap) DeleteObject(m_art_bitmap);
        m_art_bitmap = hbmp;
        m_current_art_url = art_url;
    }

    int NowPlayingService::countdown_seconds_locked(std::chrono::steady_clock::time_point now) const {
        if (m_snapshot.valid || m_pending.empty() || !m_pending.front().snap.valid) return -1;
        const auto left = m_pending.front().ready_at - now;
        if (left <= std::chrono::steady_clock::duration::zero()) return 0;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(left).count();
        return (int)((ms + 999) / 1000);  
    }

    void NowPlayingService::clear_pending_locked() {
        for (auto& p : m_pending) {
            if (p.art) DeleteObject(p.art);
        }
        m_pending.clear();
    }

    void NowPlayingService::release_due() {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto now = std::chrono::steady_clock::now();
            while (!m_pending.empty() && m_pending.front().ready_at <= now) {
                PendingUpdate& p = m_pending.front();
                m_snapshot = std::move(p.snap);
                if (p.art) {
                    if (m_art_bitmap) DeleteObject(m_art_bitmap);
                    m_art_bitmap = p.art;
                    p.art = nullptr;
                    m_current_art_url = std::move(p.art_url);
                }
                m_pending.pop_front();
                changed = true;
            }

            int countdown = -1;
            if (!m_snapshot.valid && !m_pending.empty() && m_pending.front().snap.valid) {
                countdown = countdown_seconds_locked(now);
            }
            if (countdown != m_last_countdown_s) {
                m_last_countdown_s = countdown;
                changed = true;
            }
        }
        if (changed) notify_update();
    }

    NowPlayingSnapshot NowPlayingService::snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        NowPlayingSnapshot snap = m_snapshot;
        const int countdown = countdown_seconds_locked(std::chrono::steady_clock::now());
        if (countdown >= 0) {
            snap.sync_countdown_s = countdown;
            snap.error.clear();
        }
        return snap;
    }

    HBITMAP NowPlayingService::art_bitmap() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_art_bitmap;
    }

}