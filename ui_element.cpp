#include "stdafx.h"
#include "now_playing_state.h"
#include "panel_render.h"
#include "settings.h"

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/ui_element.h>

namespace {

    azuracast::ThemeColors query_dui_theme(
        ui_element_instance_callback::ptr callback
    ) {

        if (!callback.is_valid())
            return azuracast::ThemeColors{};

        return azuracast::ThemeColors::from_raw(
            callback->is_dark_mode(),
            callback->query_std_color(ui_color_background),
            callback->query_std_color(ui_color_text),
            callback->query_std_color(ui_color_highlight)
        );
    }

    // {8BE55B7D-6945-4C9A-A159-524E58A7171D}
    static const GUID guid_azuracast_ui_element =
    { 0x8be55b7d, 0x6945, 0x4c9a, { 0xa1, 0x59, 0x52, 0x4e, 0x58, 0xa7, 0x17, 0x1d } };


    class CNowPlayingPanel
        : public CWindowImpl<CNowPlayingPanel>
        , public service_impl_t<ui_element_instance> {

    public:

        DECLARE_WND_CLASS_EX(
            L"azuracast_now_playing_panel",
            CS_DBLCLKS,
            (-1)
        )


        CNowPlayingPanel(
            ui_element_config::ptr config,
            ui_element_instance_callback::ptr callback
        )
            : m_config(config)
            , m_callback(callback) {
        }


        ~CNowPlayingPanel() {
            if (m_hWnd)
                DestroyWindow();
        }


        HWND get_wnd() override {
            return m_hWnd;
        }


        void set_configuration(
            ui_element_config::ptr config
        ) override {
            m_config = config;
        }


        ui_element_config::ptr get_configuration() override {
            return m_config;
        }


        GUID get_guid() override {
            return guid_azuracast_ui_element;
        }


        GUID get_subclass() override {
            return ui_element_subclass_utility;
        }


        BEGIN_MSG_MAP_EX(CNowPlayingPanel)

            MSG_WM_CREATE(OnCreate)

            MSG_WM_DESTROY(OnDestroy)

            MSG_WM_PAINT(OnPaint)

            MSG_WM_ERASEBKGND(OnEraseBkgnd)

            MSG_WM_SIZE(OnSize)

            MESSAGE_HANDLER_EX(
                WM_APP + 1,
                OnDataUpdated
            )

            END_MSG_MAP()


        void notify(
            const GUID& p_what,
            t_size,
            const void*,
            t_size
        ) override {

            if (
                p_what == ui_element_notify_colors_changed ||
                p_what == ui_element_notify_font_changed
                ) {
                Invalidate();
            }
        }


    private:

        LRESULT OnCreate(LPCREATESTRUCT) {

            auto& svc =
                azuracast::shared_service();

            m_update_token =
                svc.add_on_update(
                    [hwnd = m_hWnd]() {

                        if (hwnd) {
                            ::PostMessage(
                                hwnd,
                                WM_APP + 1,
                                0,
                                0
                            );
                        }
                    }
                );

            return 0;
        }


        void OnDestroy() {
            azuracast::shared_service().remove_on_update(m_update_token);
        }

        LRESULT OnDataUpdated(
            UINT,
            WPARAM,
            LPARAM
        ) {

            Invalidate();

            return 0;
        }


        BOOL OnEraseBkgnd(CDCHandle) {
            return TRUE;
        }


        void OnSize(
            UINT,
            CSize
        ) {

            Invalidate();
        }

        void OnPaint(CDCHandle) {

            CPaintDC dc(m_hWnd);

            CRect rc;
            GetClientRect(&rc);

            if (rc.Width() <= 0 || rc.Height() <= 0)
                return;

            const azuracast::ThemeColors theme =
                query_dui_theme(m_callback);

            LOGFONT base_font = {};
            const LOGFONT* base_font_ptr = nullptr;

            if (m_callback.is_valid()) {
                HFONT host_font =
                    m_callback->query_font_ex(ui_font_default);

                if (host_font &&
                    GetObject(
                        host_font,
                        sizeof(base_font),
                        &base_font
                    )) {
                    base_font_ptr = &base_font;
                }
            }

            CDC memDC;
            memDC.CreateCompatibleDC(dc);

            CBitmap memBmp;
            memBmp.CreateCompatibleBitmap(
                dc,
                rc.Width(),
                rc.Height()
            );

            HBITMAP oldBmp =
                memDC.SelectBitmap(memBmp);


            auto snap =
                azuracast::shared_service().snapshot();

            HBITMAP art =
                azuracast::shared_service().art_bitmap();

            azuracast::paint_now_playing_panel(
                memDC,
                rc,
                theme,
                snap,
                art,
                base_font_ptr
            );


            dc.BitBlt(
                0,
                0,
                rc.Width(),
                rc.Height(),
                memDC,
                0,
                0,
                SRCCOPY
            );

            memDC.SelectBitmap(oldBmp);
        }


        ui_element_config::ptr m_config;

        ui_element_instance_callback::ptr m_callback;

        azuracast::NowPlayingService::UpdateToken m_update_token = 0;
    };



    class ui_element_azuracast
        : public ui_element {

    public:

        void get_name(
            pfc::string_base& out
        ) override {

            out =
                "AzuraCast Now Playing";
        }


        GUID get_guid() override {

            return guid_azuracast_ui_element;
        }


        bool get_description(
            pfc::string_base& out
        ) override {

            out =
                "Displays now playing info from an AzuraCast station API.";

            return true;
        }


        GUID get_subclass() override {

            return ui_element_subclass_utility;
        }


        ui_element_instance::ptr instantiate(
            HWND parent,
            ui_element_config::ptr cfg,
            ui_element_instance_callback::ptr callback
        ) override {

            auto instance =
                fb2k::service_new<CNowPlayingPanel>(
                    cfg,
                    callback
                );


            if (
                !instance->Create(
                    parent,
                    NULL,
                    NULL,
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_CLIPCHILDREN |
                    WS_CLIPSIBLINGS
                )
                ) {
                return nullptr;
            }


            return instance;
        }


        ui_element_config::ptr get_default_configuration()
            override {

            return ui_element_config::g_create_empty(
                get_guid()
            );
        }


        ui_element_children_enumerator_ptr enumerate_children(
            ui_element_config::ptr cfg
        ) override {

            return nullptr;
        }
    };



    static service_factory_t<
        ui_element_azuracast
    > g_ui_element_azuracast_factory;

}
