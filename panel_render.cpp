#include "stdafx.h"
#include "panel_render.h"
#include <algorithm>
#include <vector>
#include <cwchar>

namespace azuracast {

    namespace {

        COLORREF blend_color(COLORREF from, COLORREF to, double amount) {
            amount = std::clamp(amount, 0.0, 1.0);

            auto lerp = [amount](BYTE a, BYTE b) -> BYTE {
                return static_cast<BYTE>(
                    a + (b - a) * amount + 0.5
                    );
                };

            return RGB(
                lerp(GetRValue(from), GetRValue(to)),
                lerp(GetGValue(from), GetGValue(to)),
                lerp(GetBValue(from), GetBValue(to))
            );
        }


        HFONT create_scaled_font(
            const LOGFONT* base,
            int pixel_height,
            LONG weight_override
        ) {
            LOGFONT lf = {};

            if (base) {
                lf = *base;
            }
            else {
                lf.lfCharSet = DEFAULT_CHARSET;
                lf.lfQuality = DEFAULT_QUALITY;
                lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
                wcscpy_s(lf.lfFaceName, L"Segoe UI");
            }

            lf.lfHeight = -pixel_height;
            lf.lfWidth = 0;
            lf.lfEscapement = 0;
            lf.lfOrientation = 0;
            lf.lfWeight = weight_override;
            lf.lfUnderline = FALSE;
            lf.lfStrikeOut = FALSE;

            return CreateFontIndirectW(&lf);
        }

        pfc::string8 format_time(double seconds) {
            if (seconds < 0)
                seconds = 0;

            unsigned total_sec =
                static_cast<unsigned>(seconds);

            unsigned hours =
                total_sec / 3600;

            unsigned minutes =
                (total_sec % 3600) / 60;

            unsigned secs =
                total_sec % 60;

            pfc::string8 out;

            if (hours > 0) {
                out << hours
                    << ":"
                    << pfc::format_uint(minutes, 2)
                    << ":"
                    << pfc::format_uint(secs, 2);
            }
            else {
                out << minutes
                    << ":"
                    << pfc::format_uint(secs, 2);
            }

            return out;
        }

    } 



    ThemeColors ThemeColors::from_raw(
        bool is_dark,
        COLORREF background,
        COLORREF text_primary,
        COLORREF highlight
    ) {
        ThemeColors t;

        t.is_dark = is_dark;
        t.background = background;
        t.text_primary = text_primary;

        t.progress_track =
            blend_color(
                t.text_primary,
                t.background,
                0.80
            );

        t.progress_fill = highlight;

        t.error_text =
            is_dark
            ? RGB(0xE0, 0x60, 0x60)
            : RGB(0xB0, 0x30, 0x30);

        return t;
    }


    void paint_now_playing_panel(
        HDC dc_handle,
        const CRect& rc,
        const ThemeColors& theme,
        const NowPlayingSnapshot& snap,
        HBITMAP art,
        const LOGFONT* base_font
    ) {

        CDCHandle dc(dc_handle);

        if (rc.Width() <= 0 || rc.Height() <= 0)
            return;

        dc.FillSolidRect(
            &rc,
            theme.background
        );

        dc.SetBkMode(TRANSPARENT);

        constexpr int margin = 10;
        constexpr int gap = 10;
        constexpr int bottom_area = 34;

        const int content_left = margin;
        const int content_right =
            (rc.right > static_cast<LONG>(margin))
            ? static_cast<int>(rc.right - margin)
            : content_left;

        const int content_bottom =
            (rc.bottom > static_cast<LONG>(bottom_area + margin))
            ? static_cast<int>(rc.bottom - bottom_area)
            : margin;

        const int available_w =
            std::max(1, content_right - content_left);

        const int available_h =
            std::max(1, content_bottom - margin);

        const bool horizontal =
            available_w >= 420 &&
            available_h >= 150;

        int art_natural_w = 1;
        int art_natural_h = 1;

        if (art) {
            BITMAP bm = {};

            if (GetObject(art, sizeof(bm), &bm) &&
                bm.bmWidth > 0 &&
                bm.bmHeight > 0) {
                art_natural_w = static_cast<int>(bm.bmWidth);
                art_natural_h = static_cast<int>(bm.bmHeight);
            }
        }

        int art_box_w;
        int art_box_h;

        if (horizontal) {
            const int side =
                std::max(1, std::min(available_w / 3, available_h));

            art_box_w = side;
            art_box_h = side;
        }
        else {
            art_box_h = std::max(1, available_h / 2);
            art_box_w = std::max(1, available_w);
        }

        const double art_scale =
            std::min(
                static_cast<double>(art_box_w) / art_natural_w,
                static_cast<double>(art_box_h) / art_natural_h
            );

        const int art_w =
            std::max(1, static_cast<int>(art_natural_w * art_scale + 0.5));

        const int art_h =
            std::max(1, static_cast<int>(art_natural_h * art_scale + 0.5));

        const int art_y = margin;

        const int art_x =
            horizontal
            ? content_left
            : std::max(
                content_left,
                content_left + (available_w - art_w) / 2
            );

        CRect metadataRc;

        if (horizontal) {
            metadataRc.SetRect(
                content_left + art_box_w + gap,
                art_y,
                content_right,
                art_y + art_box_h
            );
        }
        else {
            metadataRc.SetRect(
                content_left,
                art_y + art_box_h + gap,
                content_right,
                content_bottom
            );
        }

        const int song_x = metadataRc.left;
        const int song_right = metadataRc.right;
        int song_y = metadataRc.top;

        const bool compact =
            rc.Width() < 300 ||
            rc.Height() < 220;

        const int title_height = compact ? 15 : 17;
        const int artist_height = compact ? 14 : 15;
        const int small_height = compact ? 12 : 13;

        HFONT hTitleFont =
            create_scaled_font(base_font, title_height, FW_BOLD);

        HFONT hArtistFont =
            create_scaled_font(base_font, artist_height, FW_NORMAL);

        HFONT hSmallFont =
            create_scaled_font(base_font, small_height, FW_NORMAL);

        if (!snap.valid) {

            const bool syncing = snap.sync_countdown_s >= 0;

            dc.SelectFont(hSmallFont);
            dc.SetTextColor(syncing ? theme.text_primary : theme.error_text);

            CRect textRc(
                content_left,
                art_y,
                content_right,
                content_bottom
            );

            pfc::string8 msg =
                snap.error.empty()
                ? ""
                : snap.error.c_str();

            if (syncing) {
                msg = "Syncing with stream... ";
                msg << snap.sync_countdown_s << " s";
            }

            dc.DrawTextW(
                pfc::stringcvt::
                string_wide_from_utf8(
                    msg
                ).get_ptr(),
                -1,
                &textRc,
                DT_WORDBREAK |
                DT_END_ELLIPSIS
            );
        }
        else {


            if (art) {

                CDC artDC;
                artDC.CreateCompatibleDC(dc);

                HBITMAP oldArt =
                    artDC.SelectBitmap(art);

                dc.SetStretchBltMode(HALFTONE);
                SetBrushOrgEx(dc, 0, 0, nullptr);

                const int draw_y =
                    art_y + (art_box_h - art_h) / 2;

                dc.StretchBlt(
                    art_x,
                    draw_y,
                    art_w,
                    art_h,
                    artDC,
                    0,
                    0,
                    art_natural_w,
                    art_natural_h,
                    SRCCOPY
                );

                artDC.SelectBitmap(oldArt);
            }

            const int record_h = 45;

            struct RecordEntry {
                const char* text;
                HFONT font;
            };

            std::vector<RecordEntry> records;

            records.push_back({ snap.song.artist.c_str(), hArtistFont });
            records.push_back({ snap.song.title.c_str(), hTitleFont });

            if (!snap.song.album.empty()) {
                records.push_back({ snap.song.album.c_str(), hSmallFont });
            }

            const int records_avail_h =
                std::max(
                    0,
                    static_cast<int>(metadataRc.bottom) - song_y
                );

            const int slot_h =
                records.empty()
                ? 0
                : std::min(
                    record_h,
                    records_avail_h / static_cast<int>(records.size())
                );

            for (const auto& entry : records) {
                if (song_y >= metadataRc.bottom)
                    break;

                const int slot_bottom =
                    std::min<int>(metadataRc.bottom, song_y + slot_h);

                dc.SelectFont(entry.font);

                CRect measureRc(song_x, 0, song_right, slot_h);

                dc.DrawTextW(
                    pfc::stringcvt::
                    string_wide_from_utf8(entry.text).get_ptr(),
                    -1,
                    &measureRc,
                    DT_CENTER |
                    DT_WORDBREAK |
                    DT_CALCRECT |
                    DT_NOPREFIX
                );

                const int text_h =
                    std::min<int>(slot_h, measureRc.Height());

                const int text_y =
                    song_y + std::max(0, (slot_h - text_h) / 2);

                CRect recordRc(
                    song_x,
                    text_y,
                    song_right,
                    std::min<int>(slot_bottom, text_y + text_h)
                );

                dc.SetTextColor(theme.text_primary);

                dc.DrawTextW(
                    pfc::stringcvt::
                    string_wide_from_utf8(entry.text).get_ptr(),
                    -1,
                    &recordRc,
                    DT_CENTER |
                    DT_WORDBREAK |
                    DT_END_ELLIPSIS |
                    DT_NOPREFIX
                );

                song_y = slot_bottom;
            }

            const int station_name_h =
                compact ? 20 : 24;

            if (horizontal) {

                const int station_y =
                    art_y + art_box_h + gap;

                if (station_y < content_bottom) {

                    CRect stationNameRc(
                        content_left,
                        station_y,
                        content_right,
                        std::min(
                            content_bottom,
                            station_y + station_name_h
                        )
                    );

                    dc.SelectFont(hTitleFont);
                    dc.SetTextColor(theme.text_primary);

                    dc.DrawTextW(
                        pfc::stringcvt::
                        string_wide_from_utf8(
                            snap.station.name.c_str()
                        ).get_ptr(),
                        -1,
                        &stationNameRc,
                        DT_SINGLELINE |
                        DT_END_ELLIPSIS |
                        DT_NOPREFIX
                    );

                    CRect descriptionRc(
                        content_left,
                        stationNameRc.bottom + 4,
                        content_right,
                        content_bottom
                    );

                    if (descriptionRc.bottom >
                        descriptionRc.top) {

                        dc.SelectFont(hSmallFont);

                        dc.DrawTextW(
                            pfc::stringcvt::
                            string_wide_from_utf8(
                                snap.station.description.c_str()
                            ).get_ptr(),
                            -1,
                            &descriptionRc,
                            DT_WORDBREAK |
                            DT_END_ELLIPSIS |
                            DT_NOPREFIX
                        );
                    }
                }
            }
            else {

                const int station_y =
                    song_y + 4;

                if (station_y < metadataRc.bottom) {

                    CRect stationNameRc(
                        content_left,
                        station_y,
                        content_right,
                        std::min<int>(
                            metadataRc.bottom,
                            station_y + station_name_h
                        )
                    );

                    dc.SelectFont(hTitleFont);
                    dc.SetTextColor(theme.text_primary);

                    dc.DrawTextW(
                        pfc::stringcvt::
                        string_wide_from_utf8(
                            snap.station.name.c_str()
                        ).get_ptr(),
                        -1,
                        &stationNameRc,
                        DT_SINGLELINE |
                        DT_END_ELLIPSIS |
                        DT_NOPREFIX
                    );

                    CRect descriptionRc(
                        content_left,
                        stationNameRc.bottom + 4,
                        content_right,
                        metadataRc.bottom
                    );

                    if (descriptionRc.bottom >
                        descriptionRc.top) {

                        dc.SelectFont(hSmallFont);

                        dc.DrawTextW(
                            pfc::stringcvt::
                            string_wide_from_utf8(
                                snap.station.description.c_str()
                            ).get_ptr(),
                            -1,
                            &descriptionRc,
                            DT_WORDBREAK |
                            DT_END_ELLIPSIS |
                            DT_NOPREFIX
                        );
                    }
                }
            }


            if (snap.listeners >= 0) {

                dc.SelectFont(hSmallFont);
                dc.SetTextColor(theme.text_primary);

                pfc::string8 listenersText;

                listenersText
                    << snap.listeners
                    << " listeners";

                const bool narrow_bottom =
                    rc.Width() < 260;

                CRect listenersRc(
                    10,
                    rc.bottom -
                    (narrow_bottom ? 43 : 30),

                    narrow_bottom
                    ? song_right
                    : std::max(
                        song_x,
                        song_right - 140
                    ),

                    rc.bottom - 4
                );

                dc.DrawTextW(
                    pfc::stringcvt::
                    string_wide_from_utf8(
                        listenersText
                    ).get_ptr(),
                    -1,
                    &listenersRc,
                    DT_SINGLELINE |
                    DT_END_ELLIPSIS
                );
            }


            if (
                snap.elapsed >= 0 &&
                snap.duration > 0
                ) {

                dc.SelectFont(hSmallFont);
                dc.SetTextColor(theme.text_primary);

                double progress =
                    std::clamp(
                        snap.elapsed / snap.duration,
                        0.0,
                        1.0
                    );

                const LONG bar_right =
                    (rc.right >
                        static_cast<LONG>(margin))
                    ? rc.right - margin
                    : static_cast<LONG>(margin);

                CRect barRc(
                    static_cast<LONG>(margin),
                    rc.bottom - 6,
                    bar_right,
                    rc.bottom - 3
                );

                if (barRc.right > barRc.left) {

                    dc.FillSolidRect(
                        &barRc,
                        theme.progress_track
                    );

                    CRect fillRc(barRc);

                    fillRc.right =
                        fillRc.left +
                        static_cast<long>(
                            fillRc.Width() *
                            progress
                            );

                    if (fillRc.right >
                        fillRc.left) {

                        dc.FillSolidRect(
                            &fillRc,
                            theme.progress_fill
                        );
                    }
                }

                pfc::string8 elapsed_durationText;

                elapsed_durationText
                    << format_time(snap.elapsed)
                    << " / "
                    << format_time(snap.duration);

                const bool narrow_bottom =
                    rc.Width() < 260;

                CRect elapsed_durationRc;

                if (narrow_bottom) {

                    elapsed_durationRc.SetRect(
                        song_x,
                        rc.bottom - 43,
                        song_right,
                        rc.bottom - 22
                    );
                }
                else {

                    constexpr int duration_width = 130;

                    elapsed_durationRc.SetRect(
                        std::max(
                            song_x,
                            song_right -
                            duration_width
                        ),
                        rc.bottom - 30,
                        song_right,
                        rc.bottom - 4
                    );
                }

                dc.DrawTextW(
                    pfc::stringcvt::
                    string_wide_from_utf8(
                        elapsed_durationText
                    ).get_ptr(),
                    -1,
                    &elapsed_durationRc,
                    DT_SINGLELINE |
                    DT_RIGHT |
                    DT_END_ELLIPSIS
                );
            }
        }


        DeleteObject(hTitleFont);
        DeleteObject(hArtistFont);
        DeleteObject(hSmallFont);
    }

} 