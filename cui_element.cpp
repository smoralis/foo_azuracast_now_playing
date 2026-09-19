#include "stdafx.h"
#include "now_playing_state.h"
#include "panel_render.h"
#include <memory>

#include <foobar2000/SDK/foobar2000.h>
#include <../columns_ui-sdk/ui_extension.h>
#include <../columns_ui-sdk/columns_ui.h>

namespace {

    // {B266465E-F420-4F38-82B3-4CF1AF24B795}
    static const GUID guid_azuracast_cui_panel =
    { 0xb266465e, 0xf420, 0x4f38, { 0x82, 0xb3, 0x4c, 0xf1, 0xaf, 0x24, 0xb7, 0x95 } };


    azuracast::ThemeColors query_cui_theme() {

        cui::colours::helper colours(pfc::guid_null);

        return azuracast::ThemeColors::from_raw(
            colours.is_dark_mode_active(),
            colours.get_colour(cui::colours::colour_background),
            colours.get_colour(cui::colours::colour_text),
            colours.get_colour(cui::colours::colour_selection_background)
        );
    }

    class CNowPlayingCuiPanel
        : public uie::container_uie_window_v3_t<> {

    public:

        const GUID& get_extension_guid() const override {
            return guid_azuracast_cui_panel;
        }

        void get_name(pfc::string_base& out) const override {
            out = "AzuraCast Now Playing";
        }

        void get_category(pfc::string_base& out) const override {
            out = "AzuraCast";
        }

        unsigned get_type() const override {
            return uie::type_panel;
        }

        uie::container_window_v3_config get_window_config() override {
            return { L"azuracast_now_playing_cui_panel" };
        }
    


        LRESULT on_message(
            HWND wnd,
            UINT msg,
            WPARAM wp,
            LPARAM lp
        ) override {

            switch (msg) {

            case WM_CREATE:
                OnCreate(wnd);
                return 0;

            case WM_DESTROY:
                OnDestroy();
                return 0;

            case WM_ERASEBKGND:
                return TRUE;

            case WM_SIZE:
                ::InvalidateRect(wnd, nullptr, FALSE);
                return 0;

            case WM_APP + 1:
                ::InvalidateRect(wnd, nullptr, FALSE);
                return 0;

            case WM_PAINT:
                OnPaint(wnd);
                return 0;
            }

            return DefWindowProc(wnd, msg, wp, lp);
        }


    private:

        void OnCreate(HWND wnd) {

            m_update_token =
                azuracast::shared_service().add_on_update(
                    [wnd]() {

                        if (wnd) {
                            ::PostMessage(
                                wnd,
                                WM_APP + 1,
                                0,
                                0
                            );
                        }
                    }
                );


            m_dark_mode_notifier =
                std::make_unique<cui::colours::dark_mode_notifier>(
                    [wnd]() {

                        if (wnd) {
                            ::InvalidateRect(wnd, nullptr, FALSE);
                        }
                    }
                );

            m_font_callback_token =
                cui::fonts::on_common_font_changed(
                    [wnd](uint32_t) {

                        if (wnd) {
                            ::InvalidateRect(wnd, nullptr, FALSE);
                        }
                    }
                );
        }


 
        void OnDestroy() {
            azuracast::shared_service().remove_on_update(m_update_token);
            m_dark_mode_notifier.reset();
            m_font_callback_token.reset();
        }



        void OnPaint(HWND wnd) {

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(wnd, &ps);

            CRect rc;
            GetClientRect(wnd, &rc);

            if (rc.Width() > 0 && rc.Height() > 0) {

                CDCHandle dc(hdc);

                CDC memDC;
                memDC.CreateCompatibleDC(dc);

                CBitmap memBmp;
                memBmp.CreateCompatibleBitmap(
                    dc,
                    rc.Width(),
                    rc.Height()
                );

                HBITMAP oldBmp =
                    static_cast<HBITMAP>(
                        ::SelectObject(
                            static_cast<HDC>(memDC),
                            static_cast<HGDIOBJ>(static_cast<HBITMAP>(memBmp))
                        )
                        );

                auto snap =
                    azuracast::shared_service().snapshot();

                HBITMAP art =
                    azuracast::shared_service().art_bitmap();

                const LOGFONT base_font =
                    cui::fonts::get_log_font_with_fallback(
                        cui::fonts::items_font_id
                    );

                azuracast::paint_now_playing_panel(
                    memDC,
                    rc,
                    query_cui_theme(),
                    snap,
                    art,
                    &base_font
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

                ::SelectObject(
                    static_cast<HDC>(memDC),
                    static_cast<HGDIOBJ>(oldBmp)
                );
            }

            EndPaint(wnd, &ps);
        }


        azuracast::NowPlayingService::UpdateToken m_update_token = 0;
        std::unique_ptr<cui::colours::dark_mode_notifier> m_dark_mode_notifier;
        decltype(cui::fonts::on_common_font_changed(
            std::function<void(uint32_t)>()
        )) m_font_callback_token;
    };
    


    static uie::window_factory<CNowPlayingCuiPanel> g_cui_panel_factory;

}
