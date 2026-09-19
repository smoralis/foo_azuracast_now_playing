#pragma once

#include <windows.h>
#include <atlgdi.h>
#include "now_playing_state.h"


namespace azuracast {

    struct ThemeColors {
        bool is_dark = false;

        COLORREF background = RGB(0x1e, 0x1e, 0x1e);
        COLORREF text_primary = RGB(255, 255, 255);

        COLORREF progress_track = RGB(0x44, 0x44, 0x44);
        COLORREF progress_fill = RGB(0x4C, 0xAF, 0x50);

        COLORREF error_text = RGB(0xE0, 0x60, 0x60);

        static ThemeColors from_raw(
            bool is_dark,
            COLORREF background,
            COLORREF text_primary,
            COLORREF highlight
        );
    };

    void paint_now_playing_panel(
        HDC dc,
        const CRect& rc,
        const ThemeColors& theme,
        const NowPlayingSnapshot& snap,
        HBITMAP art,
        const LOGFONT* base_font = nullptr
    );

}
