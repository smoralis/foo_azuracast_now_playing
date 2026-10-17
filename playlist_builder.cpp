#include "stdafx.h"
#include "settings.h"
#include "now_playing_state.h"
#include <winhttp.h>
#include "http_client.h"
#include "json_lite.h"
#include <foobar2000/SDK/foobar2000.h>
#include <shlwapi.h>
#include <unordered_set>
#include "file_info_impl.h"

namespace {
    struct Station { std::string name, url; };

    std::wstring utf8wide(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
        return out;
    }
    std::string wideutf8(const std::wstring& s) {
        if (s.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
    std::string field(const azuracast::Json& j, const char* key) { return j[key].asString(); }

    std::vector<Station> fetch_stations() {
        std::wstring discovered = azuracast::shared_service().discovered_endpoint();
        if (discovered.empty()) {
            throw std::runtime_error("No auto-probed AzuraCast endpoint is available. Start playback of an AzuraCast stream and wait for the now-playing panel to discover its API endpoint, then retry.");
        }

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t scheme[16] = {};
        wchar_t host[512] = {};
        uc.lpszScheme = scheme; uc.dwSchemeLength = _countof(scheme);
        uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
        if (!WinHttpCrackUrl(discovered.c_str(), (DWORD)discovered.size(), 0, &uc))
            throw std::runtime_error("Could not parse the auto-probed AzuraCast endpoint URL.");
        std::wstring origin(scheme); origin += L"://"; origin += host;
        if (uc.nPort != INTERNET_DEFAULT_HTTP_PORT && uc.nPort != INTERNET_DEFAULT_HTTPS_PORT) {
            origin += L":" + std::to_wstring(uc.nPort);
        }
        const std::wstring allUrl = origin + L"/api/nowplaying";
        auto response = azuracast::http_get(allUrl);
        if (!response.success) throw std::runtime_error("AzuraCast request failed: " + response.error);
        std::string body(response.data.begin(), response.data.end());
        auto json = azuracast::Json::parse(body);
        if (json.size() == 0) throw std::runtime_error("The API returned no stations.");


        std::vector<Station> out;
        std::unordered_set<std::string> seen_urls;

        auto add_station = [&](const std::string& name,
            const std::string& url) {
                if (!url.empty() && seen_urls.insert(url).second) {
                    out.push_back({ name, url });
                }
            };

        for (size_t i = 0; i < json.size(); ++i) {
            const auto& item = json[i];
            const auto& station = item["station"];

            std::string name = field(station, "name");
            if (name.empty()) {
                name = "AzuraCast Station " + std::to_string(i + 1);
            }

            std::string url = field(item, "listen_url");
            if (url.empty()) {
                url = field(station, "listen_url");
            }
            add_station(name, url);

            const auto& mounts = station["mounts"];
            for (size_t m = 0; m < mounts.size(); ++m) {
                const auto& mount = mounts[m];
                std::string mount_url = field(mount, "url");
                std::string mount_name = field(mount, "name");

                if (mount_name.empty()) {
                    mount_name = field(mount, "path");
                }

                std::string entry_name = name + " - Mount";
                if (!mount_name.empty()) {
                    entry_name += " " + mount_name;
                }
                else {
                    entry_name += " " + std::to_string(m + 1);
                }

                add_station(entry_name, mount_url);
            }

            std::string hls_url = field(station, "hls_url");
            if (hls_url.empty()) {
                hls_url = field(item, "hls_url");
            }
            add_station(name + " (HLS)", hls_url);
        }

        if (out.empty()) {
            throw std::runtime_error(
                "No station, mount, or HLS stream URLs were found.");
        }


        return out;
    }
   
    void create_native_playlist(
        const std::vector<Station>& stations,
        const std::wstring& endpoint)
    {
        auto pm = playlist_manager::get();

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);

        wchar_t scheme[16] = {};
        wchar_t host[512] = {};

        uc.lpszScheme = scheme;
        uc.dwSchemeLength = _countof(scheme);
        uc.lpszHostName = host;
        uc.dwHostNameLength = _countof(host);

        if (!WinHttpCrackUrl(
            endpoint.c_str(),
            (DWORD)endpoint.size(),
            0,
            &uc))
        {
            throw std::runtime_error(
                "Could not parse the discovered AzuraCast endpoint.");
        }

        std::wstring base = scheme;
        base += L"://";
        base += host;

        if (uc.nPort != INTERNET_DEFAULT_HTTP_PORT &&
            uc.nPort != INTERNET_DEFAULT_HTTPS_PORT)
        {
            base += L":" + std::to_wstring(uc.nPort);
        }

        std::string base_utf8 =
            pfc::stringcvt::string_utf8_from_wide(base.c_str()).get_ptr();

        std::string playlist_name =
            std::string("AzuraCast - ") + base_utf8;

        t_size index = pm->create_playlist(
            playlist_name.c_str(),
            pfc::infinite_size,
            pfc::infinite_size);

        metadb_handle_list items;

        auto hintList = metadb_hint_list::create();
        metadb_hint_list_v2::ptr hintListV2;
        hintListV2 ^= hintList;

        if (hintListV2.is_empty()) {
            throw std::runtime_error("Browse metadata hints are not supported by this foobar2000 version.");
        }

        for (const auto& s : stations) {
            metadb_handle_ptr handle;
            playable_location_impl location(s.url.c_str(), 0);
            metadb::get()->handle_create(handle, location);

            file_info_impl info;
            const std::wstring streamName = utf8wide(s.name);
            const std::string streamNameUtf8 = wideutf8(streamName);
            info.meta_set("title", streamNameUtf8.c_str());
            info.meta_set("stream name", streamNameUtf8.c_str());

            hintListV2->add_hint_browse(handle, info, 0);
            items.add_item(handle);
        }

        hintList->on_done();

        pm->playlist_add_items(index, items, bit_array_true());
        pm->set_active_playlist(index);
    }

    class azuracast_playlist_command : public mainmenu_commands {
    public:
        t_uint32 get_command_count() override { return 1; }
        GUID get_command(t_uint32) override { return guid; }
        void get_name(t_uint32, pfc::string_base& out) override {
            out = "Create AzuraCast station playlist";
        }

        bool get_description(t_uint32, pfc::string_base& out) override {
            out = "Fetch all stations from AzuraCast and create a native foobar2000 playlist named after the server URL.";
            return true;
        }


        bool get_display(
            t_uint32 p_index,
            pfc::string_base& p_text,
            t_uint32& p_flags
        ) override {
            get_name(p_index, p_text);
            p_flags = 0;

            if (azuracast::shared_service().discovered_endpoint().empty()) {
                p_flags |= mainmenu_commands::flag_disabled;
            }

            return true;
        }


        GUID get_parent() override { return mainmenu_groups::file; }
        void execute(t_uint32, service_ptr_t<service_base>) override {
            try {
                std::wstring endpoint =
                    azuracast::shared_service().discovered_endpoint();

                if (endpoint.empty()) {
                    throw std::runtime_error(
                        "No discovered AzuraCast endpoint is available. "
                        "Start playback and wait for endpoint discovery.");
                }

                if (azuracast::shared_service().discovered_endpoint().empty()) {
                    return;
                }

                auto stations = fetch_stations();
                create_native_playlist(stations, endpoint);

                pfc::string8 message;
                message << "Created playlist with "
                    << (t_size)stations.size()
                    << " stations.";

                popup_message::g_show(message, "AzuraCast playlist");
            }
            catch (const std::exception& e) {
                popup_message::g_show(
                    pfc::string8(e.what()),
                    "AzuraCast playlist error");
            }
        }
        static constexpr GUID guid = { 0x46f9bd0a, 0x6a14, 0x4c22, {0x9a,0x65,0x2f,0x2b,0x5b,0x51,0x6c,0x83} };
    };
    static mainmenu_commands_factory_t<azuracast_playlist_command> g_playlist_command;
}
