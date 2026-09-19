#pragma once
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <cstdint>
#include "http_client.h"
#include <windows.h>
#include <gdiplus.h>

namespace azuracast {

struct StationInfo {
    std::string name;
    std::string description;
    std::string next;
    };

struct SongInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string art_url;
};

struct NowPlayingSnapshot {
    bool valid = false;
	StationInfo station;
    SongInfo song;
    int listeners = -1;
    double elapsed = -1;
    double duration = -1;
    std::string error;
    int sync_countdown_s = -1;
};

class NowPlayingService {
public:
    NowPlayingService();
    ~NowPlayingService();

    void start(const std::wstring& station_url, unsigned poll_interval_ms);
    void start_auto(const std::wstring& stream_url, unsigned poll_interval_ms);
    void stop();

    void refresh_from_preferences();

    NowPlayingSnapshot snapshot() const;

    HBITMAP art_bitmap() const;

    using UpdateToken = std::uint64_t;

    UpdateToken add_on_update(std::function<void()> cb);
    void remove_on_update(UpdateToken token);

private:
    void worker_thread(std::wstring station_url, unsigned poll_interval_ms, std::shared_ptr<HttpCancelToken> cancel);
    void auto_worker_thread(std::wstring stream_url, unsigned poll_interval_ms, std::shared_ptr<HttpCancelToken> cancel);
    std::wstring discover_station_endpoint(const std::wstring& stream_url, const std::shared_ptr<HttpCancelToken>& cancel);
    void poll_once(const std::wstring& station_url, const std::shared_ptr<HttpCancelToken>& cancel);
    void update_art(const std::string& art_url, const std::shared_ptr<HttpCancelToken>& cancel);
    HBITMAP fetch_art_bitmap(const std::string& art_url, const std::shared_ptr<HttpCancelToken>& cancel);

    void release_due();
    void clear_pending_locked();
    int countdown_seconds_locked(std::chrono::steady_clock::time_point now) const;

    void notify_update();

    mutable std::mutex m_mutex;
    NowPlayingSnapshot m_snapshot;
    HBITMAP m_art_bitmap = nullptr;
    std::string m_current_art_url;

    struct PendingUpdate {
        std::chrono::steady_clock::time_point ready_at;
        NowPlayingSnapshot snap;
        HBITMAP art = nullptr;
        std::string art_url;
    };
    std::deque<PendingUpdate> m_pending;
    std::string m_last_fetched_art_url;
    int m_last_countdown_s = -1;
    std::atomic<unsigned> m_sync_delay_ms{0};

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::shared_ptr<HttpCancelToken> m_http_cancel;

    std::map<UpdateToken, std::function<void()>> m_on_update;
    UpdateToken m_next_update_token = 1;
    ULONG_PTR m_gdiplusToken = 0;
};

    NowPlayingService& shared_service();

}