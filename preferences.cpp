#include "stdafx.h"
#include "resource.h"
#include "settings.h"
#include "now_playing_state.h"

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/preferences_page.h>
#include <foobar2000/SDK/coreDarkMode.h>

namespace {

    // {8017C16C-E3F5-4056-B893-C4B08FC9CE30}
    static const GUID guid_preferences_page =
    { 0x8017c16c, 0xe3f5, 0x4056, { 0xb8, 0x93, 0xc4, 0xb0, 0x8f, 0xc9, 0xce, 0x30 } };

    class CMyPreferences : public CDialogImpl<CMyPreferences>, public service_impl_t<preferences_page_instance> {
    public:
        CMyPreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

        enum { IDD = IDD_NOWPLAYING_CONFIG };

        HWND get_wnd() override {
            return m_hWnd;
        }

        t_uint32 get_state() override {
            t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
            if (has_changed()) state |= preferences_state::changed;
            return state;
        }

        void apply() override {
            CString url, interval;
            GetDlgItemText(IDC_STATION_URL, url);
            GetDlgItemText(IDC_POLL_INTERVAL, interval);
            const pfc::string8 new_url = pfc::string8(CT2A(url));
            const int ms = _wtoi(interval);
            const unsigned new_interval = ms > 0 ? (unsigned)ms : 10000;

            const bool url_changed = new_url != cfg_station_url.get();
            cfg_station_url = new_url;
            cfg_poll_interval = new_interval;

            if (url_changed) {
                azuracast::shared_service().refresh_from_preferences();
            }

            m_callback->on_state_changed();
        }

        void reset() override {
            SetDlgItemText(IDC_STATION_URL, L"");
            SetDlgItemText(IDC_POLL_INTERVAL, L"10000");
            m_callback->on_state_changed();
        }

        BEGIN_MSG_MAP_EX(CMyPreferences)
            MSG_WM_INITDIALOG(OnInitDialog)
            COMMAND_HANDLER_EX(IDC_STATION_URL, EN_CHANGE, OnEditChange)
            COMMAND_HANDLER_EX(IDC_POLL_INTERVAL, EN_CHANGE, OnEditChange)
            END_MSG_MAP()

    private:
        BOOL OnInitDialog(CWindow, LPARAM) {
            m_darkModeHooks.AddDialogWithControls(*this);
            SetDlgItemText(IDC_STATION_URL, pfc::stringcvt::string_wide_from_utf8(cfg_station_url.get_ptr()));
            SetDlgItemText(IDC_POLL_INTERVAL, pfc::stringcvt::string_wide_from_utf8(pfc::format_int((int)cfg_poll_interval).get_ptr()));
            return TRUE;
        }

        void OnEditChange(UINT, int, CWindow) { m_callback->on_state_changed(); }

        bool has_changed() {
            CString url, interval;
            GetDlgItemText(IDC_STATION_URL, url);
            GetDlgItemText(IDC_POLL_INTERVAL, interval);
            return pfc::string8(CT2A(url)) != cfg_station_url.get()
                || _wtoi(interval) != (int)cfg_poll_interval;
        }

        const preferences_page_callback::ptr m_callback;
        fb2k::CCoreDarkModeHooks m_darkModeHooks;
    };

    class preferences_page_myimpl : public preferences_page_v3 {
    public:
        const char* get_name() override { return "AzuraCast Now Playing"; }

        GUID get_guid() override { return guid_preferences_page; }

        GUID get_parent_guid() override { return guid_tools; }

        preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override {
            auto instance = fb2k::service_new<CMyPreferences>(callback);
            if (!instance->Create(parent)) {
                return nullptr;
            }
            return instance;
        }
    };

    static preferences_page_factory_t<preferences_page_myimpl> g_preferences_page_factory;

} 