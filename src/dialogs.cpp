#include "dialogs.h"
#include "lang.h"
#include "ui.h"
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>

// メモリ上の DLGTEMPLATE を組む。コントロールにビジュアルスタイルは当てない（クラシック表示）
struct DlgBuilder {
    std::vector<BYTE> b;
    int count = 0;
    size_t countPos = 0;

    void Align() { while (b.size() % 4) b.push_back(0); }
    void W(WORD v) { b.push_back(v & 255); b.push_back(v >> 8); }
    void D(DWORD v) { W(v & 0xffff); W(v >> 16); }
    void S(const wchar_t* s) { do W(*s); while (*s++); }

    void Begin(const wchar_t* title, int w, int h) {
        D(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU); D(0);
        countPos = b.size(); W(0);
        W(0); W(0); W((WORD)w); W((WORD)h);
        W(0); W(0); S(title);
        W(ui::FacePx() == 11 ? 8 : 9); S(ui::FaceName());   // 9pt = 12px（Tahoma のときだけ 8pt = 11px）。書体は今の言語の UI 書体
    }
    void Item(WORD atom, DWORD style, int x, int y, int w, int h, WORD id, const wchar_t* text) {
        Align();
        D(style | WS_CHILD | WS_VISIBLE); D(0);
        W((WORD)x); W((WORD)y); W((WORD)w); W((WORD)h); W(id);
        W(0xFFFF); W(atom); S(text); W(0);
        count++;
    }
    const DLGTEMPLATE* End() { *(WORD*)&b[countPos] = (WORD)count; return (const DLGTEMPLATE*)b.data(); }
};
enum { A_BUTTON = 0x0080, A_STATIC = 0x0082 };

// ---------------------------------------------------------------- 確認・通知ダイアログ（アイコン付き）
// 配置は WM_INITDIALOG でピクセル単位に決める（アイコンとサムネイルを実ピクセルで置くため）
enum { D_ICON = 300, D_THUMB, D_L1, D_L2, D_L3 };

struct MsgCtx {
    DlgIcon icon;
    std::wstring l[3];
    bool bold = false;
    bool confirm = false;
    const DlgThumb* thumb = nullptr;
    HBITMAP bmp = nullptr; int tw = 0, th = 0;
    HFONT boldFont = nullptr;
};

static int CALLBACK BoldProbe(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM p) {
    if (lf->lfWeight >= 600) *(bool*)p = true;
    return 1;
}
// UI の書体に本物の Bold があるときだけ太字にする（合成のボールドはしない）
static bool HasRealBold() {
    LOGFONTW lf = {}; lf.lfCharSet = DEFAULT_CHARSET; wcscpy_s(lf.lfFaceName, ui::FaceName());
    bool r = false; HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, BoldProbe, (LPARAM)&r, 0);
    ReleaseDC(nullptr, dc);
    return r;
}

// サムネイルを作る（最大 box x box）。透過はチェック柄の上に描く
// pixel モード：補間なし。収まるなら整数倍、収まらないなら整数分の1（n 画素ごとに1点を取る）
// 通常モード：収まらないときだけ面積平均で縮小。収まるものは整数倍の最近傍
static HBITMAP MakeThumb(const DlgThumb& t, int box, int& tw, int& th) {
    int w = t.w, h = t.h, m = std::max(w, h);
    bool avg = false; int k = 1, n = 1;
    if (m <= box) { k = box / m; tw = w * k; th = h * k; }
    else if (t.pixel) { n = (m + box - 1) / box; tw = std::max(1, w / n); th = std::max(1, h / n); }
    else { avg = true; tw = std::max(1, (int)((long long)w * box / m)); th = std::max(1, (int)((long long)h * box / m)); }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = tw; bi.bmiHeader.biHeight = -th; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    uint32_t* out = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&out, nullptr, 0);
    if (!bmp) return nullptr;
    if (!out) { DeleteObject(bmp); return nullptr; }
    for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) {
        uint32_t a, r, g, b;
        if (avg) {
            int x0 = (int)((long long)x * w / tw), x1 = std::max(x0 + 1, (int)((long long)(x + 1) * w / tw));
            int y0 = (int)((long long)y * h / th), y1 = std::max(y0 + 1, (int)((long long)(y + 1) * h / th));
            unsigned long long sa = 0, sr = 0, sg = 0, sb = 0, c = 0;
            for (int yy = y0; yy < y1 && yy < h; yy++) for (int xx = x0; xx < x1 && xx < w; xx++) {
                uint32_t v = t.px[(size_t)yy * w + xx]; sa += v >> 24; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; c++;
            }
            if (!c) c = 1;
            a = (uint32_t)((sa + c / 2) / c); r = (uint32_t)((sr + c / 2) / c); g = (uint32_t)((sg + c / 2) / c); b = (uint32_t)((sb + c / 2) / c);
        } else {
            int sx = m <= box ? x / k : std::min(w - 1, x * n + n / 2), sy = m <= box ? y / k : std::min(h - 1, y * n + n / 2);
            uint32_t v = t.px[(size_t)sy * w + sx]; a = v >> 24; r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255;
        }
        uint32_t bg = (((x >> 3) + (y >> 3)) & 1) ? 0xCC : 0xFF, ia = 255 - a;
        r = std::min(255u, r + (bg * ia + 127) / 255); g = std::min(255u, g + (bg * ia + 127) / 255); b = std::min(255u, b + (bg * ia + 127) / 255);
        out[(size_t)y * tw + x] = (r << 16) | (g << 8) | b;
    }
    return bmp;
}

static HICON LoadDlgIcon(DlgIcon i, int px, bool& own) {
    own = false;
    if (i == DlgIcon::Recycler) {
        SHSTOCKICONINFO si = { sizeof si };
        // 場所と番号を取って、指定の大きさ（32px）で読む。SHGSI_ICON だと小さい絵になることがある
        if (SUCCEEDED(SHGetStockIconInfo(SIID_RECYCLER, SHGSI_ICONLOCATION, &si))) {
            HICON hi = nullptr;
            if (PrivateExtractIconsW(si.szPath, si.iIcon, px, px, &hi, nullptr, 1, LR_DEFAULTCOLOR) == 1 && hi) { own = true; return hi; }
        }
        si = { sizeof si };
        if (SUCCEEDED(SHGetStockIconInfo(SIID_RECYCLER, SHGSI_ICON | SHGSI_LARGEICON, &si)) && si.hIcon) { own = true; return si.hIcon; }
        return LoadIconW(nullptr, IDI_QUESTION);
    }
    return (HICON)LoadImageW(nullptr, i == DlgIcon::Warning ? IDI_WARNING : IDI_ERROR, IMAGE_ICON, px, px, LR_SHARED);
}

static INT_PTR CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MsgCtx* c = (MsgCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_INITDIALOG: {
        c = (MsgCtx*)l; SetWindowLongPtrW(h, GWLP_USERDATA, l);
        int s = std::max(1, g_scale), M = 12 * s, I = 32 * s, G = 12 * s;
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        if (c->bold) {
            LOGFONTW lf; GetObjectW(f, sizeof lf, &lf); lf.lfWeight = FW_BOLD;
            c->boldFont = CreateFontIndirectW(&lf);
            if (c->boldFont) SendDlgItemMessageW(h, D_L1, WM_SETFONT, (WPARAM)c->boldFont, FALSE);
        }
        bool ownIcon;
        HICON ic = LoadDlgIcon(c->icon, I, ownIcon);
        SendDlgItemMessageW(h, D_ICON, STM_SETICON, (WPARAM)ic, 0);
        SetWindowLongPtrW(h, DWLP_USER, ownIcon ? (LONG_PTR)ic : 0);
        // サムネイル
        int thumbW = 0, thumbH = 0;
        if (c->thumb) {
            try { c->bmp = MakeThumb(*c->thumb, 96 * s, c->tw, c->th); }
            catch (...) { c->bmp = nullptr; }   // 作れなければサムネイル無しで確認を出す（ダイアログの手続きの外へ例外を出さない）
            if (c->bmp) { SendDlgItemMessageW(h, D_THUMB, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)c->bmp); thumbW = c->tw + 2; thumbH = c->th + 2; }
        }
        // 文の大きさを測る
        int textW = (thumbW ? 230 : 300) * s;
        HDC dc = GetDC(h); HFONT old = (HFONT)SelectObject(dc, f);
        int lh[3] = {}, lw[3] = {}, total = 0, maxw = 0, cnt = 0;
        for (int i = 0; i < 3; i++) {
            if (c->l[i].empty()) continue;
            if (i == 0 && c->boldFont) SelectObject(dc, c->boldFont);
            RECT r = { 0, 0, textW, 0 };
            DrawTextW(dc, c->l[i].c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
            if (i == 0 && c->boldFont) SelectObject(dc, f);
            lh[i] = r.bottom; lw[i] = r.right; total += r.bottom + (cnt ? 4 * s : 0); maxw = std::max(maxw, lw[i]); cnt++;
        }
        SelectObject(dc, old); ReleaseDC(h, dc);
        int tx = M + I + G + (thumbW ? thumbW + G : 0);
        int contentH = std::max(std::max(I, thumbH), total);
        int btnW = 75 * s, btnH = 23 * s, gap = 8 * s;
        int nb = c->confirm ? 2 : 1;
        int btnsW = nb * btnW + (nb - 1) * gap;
        int clientW = std::max(tx + maxw + M, std::max(btnsW + 2 * M, 220 * s));
        int btnY = M + contentH + 14 * s;
        int clientH = btnY + btnH + M;
        auto put = [&](int id, int x, int y, int ww, int hh) { SetWindowPos(GetDlgItem(h, id), nullptr, x, y, ww, hh, SWP_NOZORDER | SWP_NOACTIVATE); };
        put(D_ICON, M, M, I, I);
        if (thumbW) put(D_THUMB, M + I + G, M, thumbW, thumbH);
        else ShowWindow(GetDlgItem(h, D_THUMB), SW_HIDE);   // サムネイルが無いときは空の枠を出さない
        int ty = M + (contentH - total) / 2;
        const int ids[3] = { D_L1, D_L2, D_L3 };
        for (int i = 0; i < 3; i++) {
            if (c->l[i].empty()) { ShowWindow(GetDlgItem(h, ids[i]), SW_HIDE); continue; }
            SetDlgItemTextW(h, ids[i], c->l[i].c_str());
            put(ids[i], tx, ty, std::max(lw[i] + 2 * s, 1), lh[i]);
            ty += lh[i] + 4 * s;
        }
        int bx = (clientW - btnsW) / 2;
        if (c->confirm) { put(IDYES, bx, btnY, btnW, btnH); put(IDNO, bx + btnW + gap, btnY, btnW, btnH); }
        else put(IDOK, bx, btnY, btnW, btnH);
        // 窓の大きさ（クライアント → 外枠）と位置（親の中央）
        RECT wr = { 0, 0, clientW, clientH };
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        RECT pr; HWND ow = GetWindow(h, GW_OWNER);
        if (!ow || !GetWindowRect(ow, &pr)) { pr = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) }; }
        int px = (pr.left + pr.right - ww) / 2, py = (pr.top + pr.bottom - wh) / 2;
        HMONITOR mon = MonitorFromWindow(ow ? ow : h, MONITOR_DEFAULTTONEAREST); MONITORINFO mi = { sizeof mi };
        if (GetMonitorInfoW(mon, &mi)) { px = std::max((int)mi.rcWork.left, std::min(px, (int)mi.rcWork.right - ww)); py = std::max((int)mi.rcWork.top, std::min(py, (int)mi.rcWork.bottom - wh)); }
        SetWindowPos(h, nullptr, px, py, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        // 初期フォーカスは常に No（確認）／ OK（通知）
        if (c->confirm) {
            SendDlgItemMessageW(h, IDYES, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
            SendDlgItemMessageW(h, IDNO, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
            SendMessageW(h, DM_SETDEFID, IDNO, 0);
            SetFocus(GetDlgItem(h, IDNO));
            SendMessageW(h, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);   // マウスで開いても、フォーカスの印を出す
        }
        else { SetFocus(GetDlgItem(h, IDOK)); SendMessageW(h, DM_SETDEFID, IDOK, 0); }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDYES: EndDialog(h, IDYES); return TRUE;
        case IDNO: case IDCANCEL: EndDialog(h, c && c->confirm ? IDNO : IDOK); return TRUE;
        case IDOK: EndDialog(h, IDOK); return TRUE;
        }
        break;
    case WM_DESTROY:
        if (c) {
            if (c->boldFont) DeleteObject(c->boldFont);
            if (c->bmp) DeleteObject(c->bmp);
        }
        if (HICON ic = (HICON)GetWindowLongPtrW(h, DWLP_USER)) DestroyIcon(ic);
        return FALSE;
    }
    return FALSE;
}

static INT_PTR RunMsg(HWND owner, const wchar_t* title, MsgCtx& c) {
    DlgBuilder d;
    d.Begin(title, 200, 80);
    d.Item(A_STATIC, SS_ICON, 0, 0, 16, 16, D_ICON, L"");
    d.Item(A_STATIC, SS_BITMAP | SS_CENTERIMAGE | WS_BORDER, 0, 0, 16, 16, D_THUMB, L"");
    d.Item(A_STATIC, SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 0, 0, 16, 16, D_L1, L"");
    d.Item(A_STATIC, SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 0, 0, 16, 16, D_L2, L"");
    d.Item(A_STATIC, SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 0, 0, 16, 16, D_L3, L"");
    if (c.confirm) {
        d.Item(A_BUTTON, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP, 0, 0, 16, 16, IDYES, lang::T(lang::BTN_YES));
        d.Item(A_BUTTON, BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 16, 16, IDNO, lang::T(lang::BTN_NO));
    } else d.Item(A_BUTTON, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 0, 0, 16, 16, IDOK, lang::T(lang::BTN_OK));
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), d.End(), owner, MsgProc, (LPARAM)&c);
}

bool ConfirmDialog(HWND owner, DlgIcon icon, const std::wstring& l1, bool bold1, const std::wstring& l2, const std::wstring& l3, const DlgThumb* thumb) {
    MsgCtx c; c.icon = icon; c.l[0] = l1; c.l[1] = l2; c.l[2] = l3; c.confirm = true;
    c.bold = bold1 && HasRealBold();
    if (thumb && thumb->px && thumb->w > 0 && thumb->h > 0) c.thumb = thumb;
    return RunMsg(owner, lang::T(lang::DEL_TITLE), c) == IDYES;
}

void NoticeDialog(HWND owner, const std::wstring& l1, const std::wstring& l2) {
    MsgCtx c; c.icon = DlgIcon::Error; c.l[0] = l1; c.l[1] = l2;
    RunMsg(owner, L"TLIV", c);
}

// ---------------------------------------------------------------- Settings
enum { C_PIXEL = 200, C_BG0, C_BG1, C_BG2, C_BG3, C_SWATCH, C_CHOOSE, C_NOCONF, C_LANG0 = 220 };   // C_LANG0 から言語の数だけ

struct SetCtx { SettingsData* s; COLORREF custom; HBRUSH brush; };
static COLORREF g_cust16[16];

static INT_PTR CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SetCtx* c = (SetCtx*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_INITDIALOG: {
        c = (SetCtx*)l; SetWindowLongPtrW(h, GWLP_USERDATA, l);
        CheckDlgButton(h, C_PIXEL, c->s->pixel ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(h, C_BG0, C_BG3, C_BG0 + c->s->bg);
        CheckDlgButton(h, C_NOCONF, c->s->noConfirm ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(h, C_LANG0, C_LANG0 + lang::kCount - 1, C_LANG0 + c->s->lang);
        c->custom = c->s->custom; c->brush = CreateSolidBrush(c->custom);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        if ((HWND)l == GetDlgItem(h, C_SWATCH)) return (INT_PTR)c->brush;
        break;
    case WM_DESTROY: if (c && c->brush) DeleteObject(c->brush); return FALSE;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case C_CHOOSE: {
            CHOOSECOLORW cc = { sizeof cc };
            cc.hwndOwner = h; cc.lpCustColors = g_cust16; cc.rgbResult = c->custom; cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&cc)) {
                c->custom = cc.rgbResult;
                DeleteObject(c->brush); c->brush = CreateSolidBrush(c->custom);
                InvalidateRect(GetDlgItem(h, C_SWATCH), nullptr, TRUE);
                CheckRadioButton(h, C_BG0, C_BG3, C_BG3);   // 色を選んだら Custom にする
            }
            return TRUE;
        }
        case IDOK:
            c->s->pixel = IsDlgButtonChecked(h, C_PIXEL) == BST_CHECKED;
            for (int i = 0; i < 4; i++) if (IsDlgButtonChecked(h, C_BG0 + i) == BST_CHECKED) c->s->bg = i;
            c->s->custom = c->custom;
            c->s->noConfirm = IsDlgButtonChecked(h, C_NOCONF) == BST_CHECKED;
            for (int i = 0; i < lang::kCount; i++) if (IsDlgButtonChecked(h, C_LANG0 + i) == BST_CHECKED) c->s->lang = i;
            EndDialog(h, IDOK); return TRUE;
        case IDCANCEL: EndDialog(h, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}

// 配置は DLU。ただし幅は、今の言語の書体で文字の幅を測って DLU に直して決める（どの言語でも文字が切れない）
bool SettingsDialog(HWND owner, SettingsData& s) {
    using namespace lang;
    HDC dc = GetDC(nullptr);
    HFONT mf = CreateFontW(-ui::FacePx(), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_DONTCARE, ui::FaceName());
    HGDIOBJ of = SelectObject(dc, mf);
    SIZE az; GetTextExtentPoint32W(dc, L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 52, &az);
    int baseX = std::max(1, (int)((az.cx / 26 + 1) / 2));   // ダイアログの横の基本単位（DS_SETFONT の書体で決まる）
    auto tw = [&](const wchar_t* t) { SIZE z; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &z); return (int)z.cx; };
    auto dlu = [&](int px) { return (px * 4 + baseX - 1) / baseX; };
    auto cb = [&](const wchar_t* t) { return dlu(tw(t) + 20); };   // チェック・ラジオ（枠 13px + 余白）
    const wchar_t* bgName[4] = { T(SET_CHECKER), T(SET_BLACK), T(SET_WHITE), T(SET_CUSTOM) };

    int labelW = dlu(tw(T(SET_BG)) + 4), x = 14 + labelW + 2;
    int bgX[4], bgW[4];
    for (int i = 0; i < 4; i++) { bgW[i] = cb(bgName[i]); bgX[i] = x; x += bgW[i] + 2; }
    int swX = x + 2, chW = std::max(36, dlu(tw(T(SET_CHOOSE)) + 16)), chX = swX + 12 + 4;
    int endBg = chX + chW;
    int lx = 14, langX[kCount], langW[kCount];
    for (int i = 0; i < kCount; i++) { langW[i] = cb(NativeName((Lang)i)); langX[i] = lx; lx += langW[i] + 6; }
    int endLang = lx - 6;
    int ncW = cb(T(SET_NOCONF)), noteW = dlu(tw(T(SET_RESETS)) + 6);
    int content = std::max(std::max(endBg, endLang), std::max(14 + std::max(ncW, 12 + noteW), 14 + dlu(tw(T(SET_PIXEL)) + 20)));
    int gw = content + 8 - 6;                         // 枠の幅（左端 6）
    int W = std::max(6 + gw + 6, 6 + 50 + 8 + 50 + 8 + 6 + 6);
    gw = W - 12;

    DlgBuilder d;
    d.Begin(T(SET_TITLE), W, 156);
    d.Item(A_BUTTON, BS_GROUPBOX, 6, 4, gw, 52, 0xFFFF, T(SET_DISPLAY));
    d.Item(A_BUTTON, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 14, 16, cb(T(SET_PIXEL)), 10, C_PIXEL, T(SET_PIXEL));
    d.Item(A_STATIC, SS_LEFT, 14, 34, labelW, 10, 0xFFFF, T(SET_BG));
    for (int i = 0; i < 4; i++) d.Item(A_BUTTON, BS_AUTORADIOBUTTON | (i == 0 ? (WS_TABSTOP | WS_GROUP) : 0), bgX[i], 33, bgW[i], 10, (WORD)(C_BG0 + i), bgName[i]);
    d.Item(A_STATIC, SS_LEFT | WS_BORDER, swX, 32, 12, 12, C_SWATCH, L"");
    d.Item(A_BUTTON, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP, chX, 31, chW, 13, C_CHOOSE, T(SET_CHOOSE));
    d.Item(A_BUTTON, BS_GROUPBOX, 6, 58, gw, 26, 0xFFFF, T(SET_LANG));
    for (int i = 0; i < kCount; i++) d.Item(A_BUTTON, BS_AUTORADIOBUTTON | (i == 0 ? (WS_TABSTOP | WS_GROUP) : 0), langX[i], 70, langW[i], 10, (WORD)(C_LANG0 + i), NativeName((Lang)i));
    d.Item(A_BUTTON, BS_GROUPBOX, 6, 86, gw, 40, 0xFFFF, T(SET_DELETE));
    d.Item(A_BUTTON, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 14, 98, ncW, 10, C_NOCONF, T(SET_NOCONF));
    d.Item(A_STATIC, SS_LEFT, 26, 111, noteW, 10, 0xFFFF, T(SET_RESETS));
    d.Item(A_BUTTON, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, W - 6 - 50 - 8 - 50, 134, 50, 14, IDOK, T(BTN_OK));
    d.Item(A_BUTTON, BS_PUSHBUTTON | WS_TABSTOP, W - 6 - 50, 134, 50, 14, IDCANCEL, T(BTN_CANCEL));
    SelectObject(dc, of); DeleteObject(mf); ReleaseDC(nullptr, dc);

    SettingsData tmp = s;
    SetCtx c = { &tmp, 0, nullptr };
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), d.End(), owner, SettingsProc, (LPARAM)&c) != IDOK) return false;
    s = tmp;
    return true;
}
