#include "stdafx.h"
#include "history_popup.h"
#include "now_playing_state.h"
#include "../foobar2000-sdk/foobar2000/helpers/DarkMode.h"

#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "uxtheme.lib")

namespace azuracast {

    namespace {

        constexpr UINT WM_HISTORY_UPDATED = WM_APP + 2;

        constexpr int kPad = 10;
        constexpr int kTimeWidth = 52;
        constexpr int kColumnGap = 10;

        COLORREF mix(COLORREF from, COLORREF to, double amount) {
            amount = std::clamp(amount, 0.0, 1.0);

            auto lerp = [amount](BYTE a, BYTE b) -> BYTE {
                return static_cast<BYTE>(a + (b - a) * amount + 0.5);
                };

            return RGB(
                lerp(GetRValue(from), GetRValue(to)),
                lerp(GetGValue(from), GetGValue(to)),
                lerp(GetBValue(from), GetBValue(to))
            );
        }

        std::wstring to_wide(const std::string& s) {
            if (s.empty())
                return std::wstring();

            const int n = MultiByteToWideChar(
                CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);

            if (n <= 0)
                return std::wstring();

            std::wstring w(static_cast<size_t>(n), L'\0');

            MultiByteToWideChar(
                CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);

            return w;
        }

        std::wstring format_played_at(long long unix_s) {
            if (unix_s <= 0)
                return std::wstring();

            ULARGE_INTEGER u;
            u.QuadPart =
                static_cast<unsigned long long>(unix_s) * 10000000ULL +
                116444736000000000ULL;

            FILETIME ft;
            ft.dwLowDateTime = u.LowPart;
            ft.dwHighDateTime = u.HighPart;

            FILETIME local_ft;
            SYSTEMTIME st;

            if (!FileTimeToLocalFileTime(&ft, &local_ft) ||
                !FileTimeToSystemTime(&local_ft, &st))
                return std::wstring();

            wchar_t buf[16];
            swprintf_s(buf, L"%02u:%02u", st.wHour, st.wMinute);

            return buf;
        }


        class CHistoryPopup : public CWindowImpl<CHistoryPopup> {

        public:

            DECLARE_WND_CLASS_EX(
                L"azuracast_history_popup",
                CS_HREDRAW | CS_VREDRAW,
                COLOR_WINDOW
            )

            static CHistoryPopup* s_instance;

            explicit CHistoryPopup(const ThemeColors& theme)
                : m_theme(theme) {
            }

            void apply_theme(const ThemeColors& theme) {
                m_theme = theme;

                apply_scrollbar_theme();

                if (m_hWnd)
                    Invalidate(FALSE);
            }

            void apply_scrollbar_theme() {
                if (!m_hWnd)
                    return;

                ::SetWindowTheme(
                    m_hWnd,
                    m_theme.is_dark ? L"DarkMode_Explorer" : L"Explorer",
                    nullptr
                );

                ::SetWindowPos(
                    m_hWnd, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                    SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }

            void refresh(bool force) {
                const NowPlayingSnapshot snap =
                    shared_service().snapshot();

                if (!snap.valid && !force)
                    return;

                if (!force && snap.history == m_shown)
                    return;

                m_shown = snap.history;

                std::wstring title = L"Recently played";

                if (!m_shown.empty())
                    title += L" (" + std::to_wstring(m_shown.size()) + L")";

                ::SetWindowTextW(m_hWnd, title.c_str());

                update_scrollbar();
                Invalidate(FALSE);
            }

            void OnFinalMessage(HWND) override {
                s_instance = nullptr;
                delete this;
            }


            BEGIN_MSG_MAP_EX(CHistoryPopup)

                MSG_WM_CREATE(OnCreate)
                MSG_WM_DESTROY(OnDestroy)
                MSG_WM_PAINT(OnPaint)
                MSG_WM_ERASEBKGND(OnEraseBkgnd)
                MSG_WM_SIZE(OnSize)
                MSG_WM_VSCROLL(OnVScroll)
                MSG_WM_MOUSEWHEEL(OnMouseWheel)
                MSG_WM_KEYDOWN(OnKeyDown)
                MSG_WM_LBUTTONDOWN(OnLButtonDown)

                MESSAGE_HANDLER_EX(
                    WM_HISTORY_UPDATED,
                    OnHistoryUpdated
                )

                END_MSG_MAP()


        private:

            LRESULT OnCreate(LPCREATESTRUCT) {

                s_instance = this;

                NONCLIENTMETRICSW ncm = {};
                ncm.cbSize = sizeof(ncm);

                if (SystemParametersInfoW(
                    SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
                    m_font = CreateFontIndirectW(&ncm.lfMessageFont);
                }

                HFONT font = m_font
                    ? m_font
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                {
                    CClientDC dc(m_hWnd);
                    HFONT old = dc.SelectFont(font);

                    TEXTMETRIC tm = {};
                    dc.GetTextMetrics(&tm);

                    m_row_h = tm.tmHeight + 8;

                    dc.SelectFont(old);
                }

                m_dark.AddDialogWithControls(m_hWnd);

                apply_scrollbar_theme();

                m_update_token =
                    shared_service().add_on_update(
                        [hwnd = m_hWnd]() {
                            if (hwnd)
                                ::PostMessageW(hwnd, WM_HISTORY_UPDATED, 0, 0);
                        }
                    );

                refresh(true);

                return 0;
            }


            void OnDestroy() {
                shared_service().remove_on_update(m_update_token);

                if (m_font) {
                    DeleteObject(m_font);
                    m_font = nullptr;
                }
            }


            LRESULT OnHistoryUpdated(UINT, WPARAM, LPARAM) {
                refresh(false);
                return 0;
            }


            BOOL OnEraseBkgnd(CDCHandle) {
                return TRUE;
            }


            void OnSize(UINT, CSize) {
                update_scrollbar();
                Invalidate(FALSE);
            }


            int content_height() const {
                return static_cast<int>(m_shown.size()) * m_row_h;
            }

            void update_scrollbar() {
                if (!m_hWnd)
                    return;

                CRect client;
                GetClientRect(&client);

                const int total = content_height();
                const int page = client.Height();

                m_scroll = std::clamp(m_scroll, 0, std::max(0, total - page));

                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                si.nMin = 0;
                si.nMax = std::max(0, total - 1);
                si.nPage = static_cast<UINT>(std::max(0, page));
                si.nPos = m_scroll;

                SetScrollInfo(SB_VERT, &si, TRUE);
            }

            void set_scroll(int pos) {
                CRect client;
                GetClientRect(&client);

                const int max_pos =
                    std::max(0, content_height() - client.Height());

                pos = std::clamp(pos, 0, max_pos);

                if (pos == m_scroll)
                    return;

                m_scroll = pos;
                update_scrollbar();
                Invalidate(FALSE);
            }

            void OnVScroll(int code, short, HWND) {
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;
                GetScrollInfo(SB_VERT, &si);

                int pos = si.nPos;

                switch (code) {
                case SB_LINEUP:        pos -= m_row_h; break;
                case SB_LINEDOWN:      pos += m_row_h; break;
                case SB_PAGEUP:        pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN:      pos += static_cast<int>(si.nPage); break;
                case SB_TOP:           pos = 0; break;
                case SB_BOTTOM:        pos = content_height(); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                default: return;
                }

                set_scroll(pos);
            }

            BOOL OnMouseWheel(UINT, short delta, CPoint) {
                set_scroll(m_scroll - (delta / WHEEL_DELTA) * m_row_h * 3);
                return TRUE;
            }

            void OnKeyDown(UINT key, UINT, UINT) {
                CRect client;
                GetClientRect(&client);

                switch (key) {
                case VK_UP:     set_scroll(m_scroll - m_row_h); break;
                case VK_DOWN:   set_scroll(m_scroll + m_row_h); break;
                case VK_PRIOR:  set_scroll(m_scroll - client.Height()); break;
                case VK_NEXT:   set_scroll(m_scroll + client.Height()); break;
                case VK_HOME:   set_scroll(0); break;
                case VK_END:    set_scroll(content_height()); break;
                case VK_ESCAPE: DestroyWindow(); break;
                default: SetMsgHandled(FALSE); break;
                }
            }

            void OnLButtonDown(UINT, CPoint) {
                SetFocus();
            }



            void OnPaint(CDCHandle) {

                CPaintDC dc(m_hWnd);

                CRect rc;
                GetClientRect(&rc);

                if (rc.Width() <= 0 || rc.Height() <= 0)
                    return;

                CDC memDC;
                memDC.CreateCompatibleDC(dc);

                CBitmap memBmp;
                memBmp.CreateCompatibleBitmap(dc, rc.Width(), rc.Height());

                HBITMAP oldBmp = memDC.SelectBitmap(memBmp);

                memDC.FillSolidRect(&rc, m_theme.background);
                memDC.SetBkMode(TRANSPARENT);

                HFONT font = m_font
                    ? m_font
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                HFONT oldFont = memDC.SelectFont(font);

                if (m_shown.empty()) {

                    memDC.SetTextColor(
                        mix(m_theme.text_primary, m_theme.background, 0.4));

                    CRect msgRc(rc);

                    memDC.DrawTextW(
                        L"No history available",
                        -1,
                        &msgRc,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                    );
                }
                else {

                    const COLORREF dim =
                        mix(m_theme.text_primary, m_theme.background, 0.4);

                    const COLORREF stripe =
                        mix(m_theme.text_primary, m_theme.background, 0.95);

                    const int n = static_cast<int>(m_shown.size());
                    const int first = m_scroll / m_row_h;

                    int y = first * m_row_h - m_scroll;

                    for (int i = first; i < n && y < rc.bottom; ++i, y += m_row_h) {

                        CRect rowRc(0, y, rc.right, y + m_row_h);

                        if (i % 2)
                            memDC.FillSolidRect(&rowRc, stripe);

                        const std::wstring when =
                            format_played_at(m_shown[i].played_at);

                        CRect timeRc(kPad, y, kPad + kTimeWidth, y + m_row_h);

                        memDC.SetTextColor(dim);

                        memDC.DrawTextW(
                            when.c_str(),
                            -1,
                            &timeRc,
                            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                        );

                        const std::wstring text = to_wide(m_shown[i].text);

                        CRect textRc(
                            kPad + kTimeWidth + kColumnGap,
                            y,
                            std::max<int>(
                                kPad + kTimeWidth + kColumnGap,
                                rc.right - kPad),
                            y + m_row_h
                        );

                        memDC.SetTextColor(m_theme.text_primary);

                        memDC.DrawTextW(
                            text.c_str(),
                            -1,
                            &textRc,
                            DT_SINGLELINE | DT_VCENTER |
                            DT_END_ELLIPSIS | DT_NOPREFIX
                        );
                    }
                }

                memDC.SelectFont(oldFont);

                dc.BitBlt(0, 0, rc.Width(), rc.Height(), memDC, 0, 0, SRCCOPY);

                memDC.SelectBitmap(oldBmp);
            }


            fb2k::CDarkModeHooks m_dark;
            ThemeColors m_theme;
            HFONT m_font = nullptr;
            int m_row_h = 22;
            int m_scroll = 0;
            std::vector<HistoryEntry> m_shown;
            NowPlayingService::UpdateToken m_update_token = 0;
        };

        CHistoryPopup* CHistoryPopup::s_instance = nullptr;

    }


    void show_history_popup(const ThemeColors& theme) {

        if (CHistoryPopup::s_instance &&
            ::IsWindow(CHistoryPopup::s_instance->m_hWnd)) {

            CHistoryPopup* p = CHistoryPopup::s_instance;

            p->apply_theme(theme);
            p->refresh(true);

            if (::IsIconic(p->m_hWnd))
                ::ShowWindow(p->m_hWnd, SW_RESTORE);

            ::SetForegroundWindow(p->m_hWnd);
            return;
        }

        CHistoryPopup* popup = new CHistoryPopup(theme);

        const HWND owner = core_api::get_main_window();

        CRect rc(0, 0, 520, 380);

        if (!popup->Create(
            owner,
            rc,
            L"Recently played",
            (WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) |
            WS_CLIPCHILDREN | WS_VSCROLL,
            WS_EX_DLGMODALFRAME
        )) {
            delete popup;
            return;
        }

        popup->CenterWindow(owner);
        popup->ShowWindow(SW_SHOW);
        popup->SetFocus();
    }

}