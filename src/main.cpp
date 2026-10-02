#include "common.h"
#include "version.h"
#include "ui.h"
#include "viewer.h"
#include "decode.h"
#include "filelist.h"
#include "dialogs.h"
#include "lang.h"
#include "infopanel.h"
#include "settings.h"
#include "clipboard.h"
#include "delete.h"
#include "util.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <new>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "uuid.lib")

IWICImagingFactory* g_wic = nullptr;

static const UINT WM_DIRCHANGED = WM_APP + 1;
static const UINT_PTR TM_FLASH = 1, TM_DIR = 2, TM_RETRY = 3;

static HWND g_hwnd = nullptr, g_tip = nullptr;
static FileList g_fl;
static UiState g_ui;
static std::wstring g_flash;
static std::wstring g_loadedPath;
static FILETIME g_loadedMtime{};
static ULONGLONG g_loadedSize = 0;
static int g_retry = 0;
static std::wstring g_watchDir;
static bool g_noConfirm = false;   // 確認なしで削除。ini には保存せず、起動のたびに OFF

// 情報パネル。幅は論理px（仕切り4pxを含む）。g_infoW は保存値で、窓が狭いときの表示だけ縮める
static const int kInfoId = 900, kInfoMin = 120, kViewMin = 200;
static bool g_infoOn = false;
static int g_infoW = settings::kInfoWidthDefault;
static bool g_splitDrag = false;
static int g_splitGrab = 0;

// ---------------------------------------------------------------- 設定
static void SaveSettings() {
    WINDOWPLACEMENT wp = { sizeof wp }; GetWindowPlacement(g_hwnd, &wp);
    settings::Data d;
    d.pixel = viewer::Pixel(); d.bg = viewer::Bg(); d.custom = viewer::Custom();
    d.x = wp.rcNormalPosition.left; d.y = wp.rcNormalPosition.top;
    d.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    d.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    d.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    d.infoOn = g_infoOn; d.infoW = g_infoW; d.lang = lang::Get();
    settings::Save(d);
}

// ---------------------------------------------------------------- ステータス
static std::wstring FmtZoom(float z) {
    double v = z * 100; wchar_t b[32];
    if (z >= 1) swprintf_s(b, L"%d%%", (int)std::lround(v));
    else if (std::fabs(v - std::round(v)) < 1e-3) swprintf_s(b, L"%d%%", (int)std::lround(v));
    else swprintf_s(b, L"%.1f%%", v);
    return b;
}

static void InvalidateChrome();
static void DoLayout();
static void RefreshStatus() {
    if (!g_hwnd) return;
    StatusInfo s;
    bool have = g_fl.cur >= 0 && g_fl.cur < (int)g_fl.paths.size();
    wchar_t b[64];
    if (have) {
        s.name = g_flash.empty() ? NameOf(g_fl.paths[g_fl.cur]) : g_flash;
        swprintf_s(b, L"%d/%d", g_fl.cur + 1, (int)g_fl.paths.size()); s.index = b;
    } else s.name = g_flash;
    const Image* im = viewer::Cur();
    if (im) {
        s.basic = true;
        swprintf_s(b, L"%dx%d", im->w, im->h); s.size = b;
        if (viewer::Animated()) { swprintf_s(b, L"%d/%d", viewer::FrameIndex() + 1, viewer::FrameCount()); s.frame = b; }
        s.zoom = FmtZoom(viewer::Zoom());
        s.pixel = viewer::Pixel();
        s.mode = lang::T(s.pixel ? lang::ST_PIXEL : lang::ST_NORMAL);
        if (s.pixel) {
            int w, h, x, y; uint32_t px;
            if (viewer::SelDragging(w, h)) { swprintf_s(b, L"%dx%d", w, h); s.pos = b; }
            else if (viewer::Probe(x, y, px)) { swprintf_s(b, L"%d, %d", x, y); s.pos = b; }
            else s.pos = L"-";
            if (viewer::Probe(x, y, px)) {
                uint32_t u = Unpremultiply(px), a = u >> 24;
                if (a == 0) s.color = lang::T(lang::ST_TRANSPARENT);
                else {
                    uint32_t r = (u >> 16) & 255, g = (u >> 8) & 255, bl = u & 255;
                    swprintf_s(b, L"#%02X%02X%02X", r, g, bl); s.color = b;
                    if (a < 255) { swprintf_s(b, L" a%u", a); s.color += b; }
                    s.hasSwatch = true; s.swatch = RGB(r, g, bl);
                }
            } else s.color = L"-";
        }
    }
    bool pixelOn = viewer::Pixel();
    if (!(s == g_ui.status)) {
        g_ui.status = s;
        RECT r = ui::StatusRect();
        r = { r.left * g_scale, r.top * g_scale, r.right * g_scale, r.bottom * g_scale };
        InvalidateRect(g_hwnd, &r, FALSE);
    }
    bool selOn = viewer::SelectMode(), playOn = viewer::Playing(), anim = viewer::Animated();
    if (pixelOn != g_ui.pixelOn || g_infoOn != g_ui.infoOn || selOn != g_ui.selectOn || playOn != g_ui.playOn || anim != g_ui.animated) {
        g_ui.pixelOn = pixelOn; g_ui.infoOn = g_infoOn; g_ui.selectOn = selOn; g_ui.playOn = playOn; g_ui.animated = anim;
        if (!anim) g_ui.hoverTool = g_ui.pressTool = -1;
        InvalidateChrome();
    }
    std::wstring title = have ? NameOf(g_fl.paths[g_fl.cur]) + L" - TLIV" : L"TLIV";
    static std::wstring lastTitle;
    if (title != lastTitle) { lastTitle = title; SetWindowTextW(g_hwnd, title.c_str()); }
}

void OnViewChanged() { RefreshStatus(); }

static void InvalidateChrome() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    ui::Layout L = ui::Calc(rc.right / g_scale, rc.bottom / g_scale);
    RECT r = { 0, 0, rc.right, L.toolbar.bottom * g_scale };
    InvalidateRect(g_hwnd, &r, FALSE);
}

static void Flash(const std::wstring& t) {
    g_flash = t; SetTimer(g_hwnd, TM_FLASH, 1200, nullptr); RefreshStatus();
}

// ---------------------------------------------------------------- 画像を開く
static void NoteLoaded(const std::wstring& path) {
    g_loadedPath = path;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) { g_loadedMtime = fa.ftLastWriteTime; g_loadedSize = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow; }
    else { g_loadedMtime = FILETIME{}; g_loadedSize = 0; }
}

// 情報パネルの中身を作り直す。隠れているときは何もしない（出したときに作る）
static void UpdateInfo() {
    if (!g_infoOn) return;
    if (g_fl.cur < 0 || g_fl.cur >= (int)g_fl.paths.size()) { infopanel::Clear(); return; }
    InfoReq rq; rq.path = g_fl.paths[g_fl.cur];
    if (const Image* im = viewer::Cur()) {
        rq.decoded = true; rq.svg = im->svg; rq.w = im->w; rq.h = im->h; rq.frames = (int)im->frames.size();
        rq.orient = im->orient; rq.framesTotal = im->framesTotal; rq.memLimited = im->memLimited; rq.frameLimited = im->frameLimited;
        for (auto& f : im->frames) rq.totalMs += f.delay;
    }
    infopanel::Request(rq);
}

// 画像を読んで表示する（デコードと SetImage を含む）。メモリが足りなければ、表示中の画像を手放して（viewer::Clear）からもう1回だけ試す。
// それでもだめなら false（呼び出し側が「開けない」を出す）。上限で切ったかは memLim / frameLim に返す
static bool LoadAndShow(const std::wstring& p, bool keep, bool& memLim, bool& frameLim, int& shown) {
    for (int attempt = 0; attempt < 2; attempt++) {
        try {
            auto img = std::make_unique<Image>();
            bool decoded = DecodeFile(p, *img);
            memLim = decoded && img->memLimited; frameLim = decoded && img->frameLimited; shown = decoded ? (int)img->frames.size() : 0;
            return decoded && viewer::SetImage(std::move(img), keep);
        } catch (const std::bad_alloc&) {
            if (attempt == 0) viewer::Clear();
        }
    }
    return false;
}

static void ShowCurrent(bool keep) {
    KillTimer(g_hwnd, TM_RETRY);
    if (g_fl.cur < 0 || g_fl.cur >= (int)g_fl.paths.size()) { viewer::Clear(); g_loadedPath.clear(); UpdateInfo(); RefreshStatus(); return; }
    const std::wstring& p = g_fl.paths[g_fl.cur];
    NoteLoaded(p);
    bool memLim = false, frameLim = false; int shown = 0;
    if (LoadAndShow(p, keep, memLim, frameLim, shown)) {
        // 上限で切ったとき、1.2秒だけ出す
        if (memLim) { wchar_t mb[96]; swprintf_s(mb, lang::T(lang::MEMLIMIT), shown); Flash(mb); }
        else if (frameLim) { wchar_t mb[96]; swprintf_s(mb, lang::T(lang::FRAMELIMIT), shown); Flash(mb); }
    }
    else viewer::SetFailed();
    UpdateInfo();
    RefreshStatus();
}

static void OpenPath(const std::wstring& path) {
    // 必要な長さを先に聞いてから入れ物を用意する。失敗したら何もしない
    DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!need) return;
    std::vector<wchar_t> buf(need);
    DWORD got = GetFullPathNameW(path.c_str(), need, buf.data(), nullptr);
    if (!got || got >= need) return;
    std::wstring full(buf.data(), got);
    if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    BuildList(full, g_fl);
    if (g_fl.dir != g_watchDir) { g_watchDir = g_fl.dir; StartWatch(g_hwnd, WM_DIRCHANGED, g_watchDir); }
    ShowCurrent(false);
}

void NavigateRel(int d) {
    int n = (int)g_fl.paths.size();
    if (n == 0) return;
    g_fl.cur = ((g_fl.cur + d) % n + n) % n;
    ShowCurrent(false);
}

void OnFilesDropped(HDROP h) {
    UINT n = DragQueryFileW(h, 0, nullptr, 0);   // 文字数（終端を除く）を先に聞く
    if (n) {
        std::vector<wchar_t> p((size_t)n + 1);
        UINT got = DragQueryFileW(h, 0, p.data(), n + 1);
        if (got) OpenPath(std::wstring(p.data(), got));
    }
    DragFinish(h);
}

// フォルダ変化・ファイル更新
static void OnDirChanged() {
    if (g_fl.paths.empty()) return;
    std::wstring before = g_loadedPath;
    RefreshList(g_fl);
    if (g_fl.cur < 0) { ShowCurrent(false); return; }
    if (_wcsicmp(g_fl.paths[g_fl.cur].c_str(), before.c_str())) { ShowCurrent(false); return; }
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(before.c_str(), GetFileExInfoStandard, &fa)) {
        ULONGLONG sz = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        if (CompareFileTime(&fa.ftLastWriteTime, &g_loadedMtime) != 0 || sz != g_loadedSize) {
            bool memLim = false, frameLim = false; int shown = 0;
            if (LoadAndShow(before, true, memLim, frameLim, shown)) { NoteLoaded(before); g_retry = 0; UpdateInfo(); }
            else if (g_retry++ < 10) SetTimer(g_hwnd, TM_RETRY, 300, nullptr);   // 書き込み途中かもしれない
            else { NoteLoaded(before); viewer::SetFailed(); UpdateInfo(); }
        }
    }
    RefreshStatus();
}

// ---------------------------------------------------------------- コマンド
static void DoCopy() {
    const Image* im = viewer::Cur();
    if (!im) return;
    RECT r = viewer::CropRect();
    CopyResult cr;
    try { cr = CopyToClipboard(g_hwnd, viewer::CurPixels().data(), im->w, r); }
    catch (const std::bad_alloc&) { cr = CopyResult::NoMemory; }
    if (cr == CopyResult::Ok) {
        wchar_t b[64]; swprintf_s(b, lang::T(lang::COPIED), (int)(r.right - r.left), (int)(r.bottom - r.top));
        Flash(b);
    } else if (cr == CopyResult::NoMemory) Flash(lang::T(lang::COPY_FAILED));
}

static void DoOpenDialog() {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW o = { sizeof o };
    o.hwndOwner = g_hwnd;
    std::wstring filter = OpenFileFilter();
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf; o.nMaxFile = MAX_PATH * 2; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&o)) OpenPath(buf);
}

// 一覧から今の画像を外して、次（なければ前）を表示する
static void DropCurrent() {
    g_fl.paths.erase(g_fl.paths.begin() + g_fl.cur);
    if (g_fl.paths.empty()) g_fl.cur = -1;
    else if (g_fl.cur >= (int)g_fl.paths.size()) g_fl.cur = (int)g_fl.paths.size() - 1;
    ShowCurrent(false);
}

static void DoDelete() {
    if (g_fl.cur < 0 || g_fl.cur >= (int)g_fl.paths.size()) return;
    std::wstring path = g_fl.paths[g_fl.cur];
    switch (DeleteWithConfirm(g_hwnd, path, g_loadedPath, g_noConfirm)) {
    case DeleteResult::Deleted: DropCurrent(); Flash(lang::Sub(lang::DELETED, NameOf(path))); break;
    case DeleteResult::AlreadyGone: DropCurrent(); Flash(lang::T(lang::GONE)); break;
    case DeleteResult::Kept: break;
    }
}

static void UpdateTips();
static void DoLayout();

// 言語を切り替える：書体を作り直し、バー・ツールチップ・ステータス・「開けない」の表示を描き直す（再起動しない）
static void ApplyLanguage(lang::Lang l) {
    lang::Set(l);
    settings::SaveLanguage(l);
    g_flash.clear(); KillTimer(g_hwnd, TM_FLASH);
    ui::RebuildFont();
    infopanel::SetFont(g_scale);
    viewer::OnScaleChanged();   // 「開けない」の文字のキャッシュを捨てる
    g_ui.hoverTool = g_ui.hoverMenu = -1;
    DoLayout();                 // ツールチップの更新と全体の描き直しを含む
    RefreshStatus();
}

static void DoSettings() {
    SettingsData s; s.pixel = viewer::Pixel(); s.bg = viewer::Bg(); s.custom = viewer::Custom(); s.noConfirm = g_noConfirm; s.lang = lang::Get();
    if (!SettingsDialog(g_hwnd, s)) return;
    viewer::SetCustom(s.custom); viewer::SetBg(s.bg); viewer::SetPixel(s.pixel);
    g_noConfirm = s.noConfirm;
    if (s.lang != (int)lang::Get()) ApplyLanguage((lang::Lang)s.lang);
}

static void Command(int id) {
    switch (id) {
    case ID_OPEN: DoOpenDialog(); break;
    case ID_SHOWEXP:
        if (g_fl.cur >= 0 && g_fl.cur < (int)g_fl.paths.size()) {
            std::wstring a = L"/select,\"" + g_fl.paths[g_fl.cur] + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case ID_SETTINGS: DoSettings(); break;
    case ID_DELETE: DoDelete(); break;
    case ID_EXIT: DestroyWindow(g_hwnd); break;
    case ID_COPY: DoCopy(); break;
    case ID_SELALL: viewer::SetSelectMode(true); viewer::SelectAll(); break;   // Ctrl+A は範囲選択をオンにしてから全体を選ぶ
    case ID_SELMODE: viewer::SetSelectMode(!viewer::SelectMode()); break;
    case ID_PLAY: viewer::TogglePlay(); break;
    case ID_FPREV: viewer::StepFrame(-1); break;
    case ID_FNEXT: viewer::StepFrame(1); break;
    case ID_DESEL: viewer::Deselect(); break;
    case ID_PIXEL: viewer::SetPixel(!viewer::Pixel()); break;
    case ID_INFO:
        g_infoOn = !g_infoOn;
        DoLayout();
        if (g_infoOn) UpdateInfo(); else SetFocus(g_hwnd);
        RefreshStatus();
        break;
    case ID_ZOOMIN: viewer::ZoomStep(1); break;
    case ID_ZOOMOUT: viewer::ZoomStep(-1); break;
    case ID_RESET: viewer::Reset(); break;
    case ID_BGCYCLE: viewer::SetBg(viewer::Bg() + 1); break;
    case ID_PREV: NavigateRel(-1); break;
    case ID_NEXT: NavigateRel(1); break;
    case ID_KEYS: MessageBoxW(g_hwnd, lang::T(lang::KEYS_BODY), lang::T(lang::KEYS_TITLE), MB_OK); break;
    case ID_ABOUT: MessageBoxW(g_hwnd, L"TLIV " TLIV_VER_WSTR L"\nTenokun's Light Image Viewer\nMIT License", lang::T(lang::ABOUT_TITLE), MB_OK); break;
    }
}

// ---------------------------------------------------------------- 窓
static void OpenMenu(int i) {
    RECT r = ui::MenuItemRect(i);
    POINT p = { r.left * g_scale, r.bottom * g_scale }; ClientToScreen(g_hwnd, &p);
    HMENU m = ui::BuildMenu(i);
    g_ui.openMenu = i; InvalidateChrome(); UpdateWindow(g_hwnd);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    g_ui.openMenu = -1; g_ui.hoverMenu = -1; InvalidateChrome();
}

static void UpdateTips() {
    if (!g_tip) return;
    for (int i = 0; i < ui::ToolCount(); i++) {
        TOOLINFOW ti = { TTTOOLINFOW_V1_SIZE };   /* comctl32 v5 は V1 のサイズしか受けない */ ti.hwnd = g_hwnd; ti.uId = i;
        RECT r = ui::ToolRect(i);
        r = { r.left * g_scale, r.top * g_scale, r.right * g_scale, r.bottom * g_scale };
        if (!SendMessageW(g_tip, TTM_GETTOOLINFOW, 0, (LPARAM)&ti)) {
            ti.uFlags = TTF_SUBCLASS; ti.rect = r; ti.lpszText = (LPWSTR)ui::ToolTip(i);
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        } else { ti.rect = r; SendMessageW(g_tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti); ti.lpszText = (LPWSTR)ui::ToolTip(i); SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti); }
    }
    // ツールチップも UI と同じ書体（言語か倍率が変わったときだけ作り直す）
    static HFONT tipFont = nullptr; static int tipLang = -1, tipScale = 0;
    if (!tipFont || tipLang != (int)lang::Get() || tipScale != g_scale) {
        HFONT nf = ui::CreateUiFont(g_scale);
        SendMessageW(g_tip, WM_SETFONT, (WPARAM)nf, 0);
        if (tipFont) DeleteObject(tipFont);
        tipFont = nf; tipLang = (int)lang::Get(); tipScale = g_scale;
    }
}

// 実際に使うパネル幅。保存値は変えず、窓が狭いときだけ縮める（最小 120、最大は窓の幅 - 200）
static int EffInfoW(int lw) { return std::max(1, std::min(std::max(g_infoW, kInfoMin), lw - kViewMin)); }

static void DoLayout() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int lw = rc.right / g_scale, lh = rc.bottom / g_scale;   // バー類は1倍の論理座標で扱う
    ui::SetSize(lw, lh);
    ui::SetPanel(g_infoOn, EffInfoW(lw));
    ui::Layout L = ui::Calc(lw, lh);
    if (g_infoOn) {
        RECT p = L.panel; int in = 4;   // 枠の内側に EDIT を置く（表示領域と同じ余白）
        infopanel::Place((p.left + in) * g_scale, (p.top + in) * g_scale, std::max(1, (int)(p.right - p.left - 2 * in) * g_scale), std::max(1, (int)(p.bottom - p.top - 2 * in) * g_scale));
    }
    infopanel::Show(g_infoOn);
    viewer::Move(L.view.left * g_scale, L.view.top * g_scale, std::max(1L, (L.view.right - L.view.left) * g_scale), std::max(1L, (L.view.bottom - L.view.top) * g_scale));
    UpdateTips();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;
        g_scale = std::max(1, (int)(GetDpiForWindow(h) / 96));
        ui::RebuildFont();
        if (!viewer::Create(h)) return -1;
        if (!infopanel::Create(h, kInfoId)) return -1;
        g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        return 0;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) DoLayout();
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mi = (MINMAXINFO*)l; {   // ツールバーのボタンが全部見える幅（外枠込み）
            RECT r = { 0, 0, ui::MinClientWidth() * g_scale, 0 };
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, GetDpiForWindow(h));
            mi->ptMinTrackSize.x = r.right - r.left;
        }
        mi->ptMinTrackSize.y = 240 * g_scale; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int lw = rc.right / g_scale, lh = rc.bottom / g_scale;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, lw, lh);
        HGDIOBJ old = SelectObject(mem, bmp);
        ui::Paint(mem, lw, lh, g_ui);
        if (g_scale == 1) BitBlt(hdc, 0, 0, lw, lh, mem, 0, 0, SRCCOPY);
        else {
            // 1倍で描いたものを最近傍で整数倍に拡大（文字も滲まない）
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchBlt(hdc, 0, 0, lw * g_scale, lh * g_scale, mem, 0, 0, lw, lh, SRCCOPY);
        }
        // 割り切れない端数の余白は面の色で塗る
        HBRUSH fb = CreateSolidBrush(RGB(0xc0, 0xc0, 0xc0));
        RECT e1 = { lw * g_scale, 0, rc.right, rc.bottom }, e2 = { 0, lh * g_scale, rc.right, rc.bottom };
        FillRect(hdc, &e1, fb); FillRect(hdc, &e2, fb); DeleteObject(fb);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        g_scale = std::max(1, (int)(HIWORD(w) / 96));
        ui::RebuildFont(); viewer::OnScaleChanged(); infopanel::SetFont(g_scale);
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        DoLayout();
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && g_infoOn) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            RECT rc; GetClientRect(h, &rc);
            ui::Layout L = ui::Calc(rc.right / g_scale, rc.bottom / g_scale);
            POINT q = { p.x / g_scale, p.y / g_scale };
            if (g_splitDrag || PtInRect(&L.splitter, q)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l) / g_scale, y = GET_Y_LPARAM(l) / g_scale;
        if (g_splitDrag) {
            RECT rc; GetClientRect(h, &rc); int lw = rc.right / g_scale;
            int nw = (lw - 2) - (x - g_splitGrab);   // 右端（余白2）から仕切りの左端まで
            nw = std::max(kInfoMin, std::min(nw, lw - kViewMin));
            if (nw != g_infoW) { g_infoW = nw; DoLayout(); }
            return 0;
        }
        int t = ui::HitTool(x, y), mn = ui::HitMenu(x, y);
        if (t >= 0 && !ui::ToolEnabled(t, g_ui.animated)) t = -1;
        if (t != g_ui.hoverTool || mn != g_ui.hoverMenu) { g_ui.hoverTool = t; g_ui.hoverMenu = mn; InvalidateChrome(); }
        TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_ui.hoverTool != -1 || g_ui.hoverMenu != -1) { g_ui.hoverTool = g_ui.hoverMenu = -1; InvalidateChrome(); }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l) / g_scale, y = GET_Y_LPARAM(l) / g_scale;
        SetFocus(h);
        if (g_infoOn) {
            RECT rc; GetClientRect(h, &rc);
            ui::Layout L = ui::Calc(rc.right / g_scale, rc.bottom / g_scale); POINT q = { x, y };
            if (PtInRect(&L.splitter, q)) { g_splitDrag = true; g_splitGrab = x - L.splitter.left; SetCapture(h); return 0; }
        }
        int mn = ui::HitMenu(x, y);
        if (mn >= 0) { OpenMenu(mn); return 0; }
        int t = ui::HitTool(x, y);
        if (t >= 0 && ui::ToolEnabled(t, g_ui.animated)) { g_ui.pressTool = t; SetCapture(h); InvalidateChrome(); }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_splitDrag) { g_splitDrag = false; ReleaseCapture(); return 0; }
        if (g_ui.pressTool >= 0) {
            int t = ui::HitTool(GET_X_LPARAM(l) / g_scale, GET_Y_LPARAM(l) / g_scale), p = g_ui.pressTool;
            g_ui.pressTool = -1; ReleaseCapture(); InvalidateChrome();
            if (t == p) Command(ui::ToolCmd(p));
        }
        return 0;
    case WM_CAPTURECHANGED: g_splitDrag = false; if (g_ui.pressTool >= 0) { g_ui.pressTool = -1; InvalidateChrome(); } return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) {
            // Alt+下線の文字でメニューを開く。Alt 単独は無視
            wchar_t c = towupper((wchar_t)l);
            static const wchar_t letters[] = L"FEVH";
            for (int i = 0; i < 4; i++) if (l && c == letters[i]) { OpenMenu(i); return 0; }
            return 0;
        }
        break;
    case WM_COMMAND: if (!l) Command(LOWORD(w)); return 0;   // 子のコントロール（情報パネル）の通知は無視
    case WM_CTLCOLORSTATIC:   // 読み取り専用の EDIT は既定で灰色になるので、白地に黒字にする
        if ((HWND)l == infopanel::Hwnd()) { SetTextColor((HDC)w, RGB(0, 0, 0)); SetBkColor((HDC)w, RGB(255, 255, 255)); return (LRESULT)GetStockObject(WHITE_BRUSH); }
        break;
    case infopanel::WM_INFO_TEXT: infopanel::OnText((infopanel::InfoMsg*)l); return 0;
    case infopanel::WM_INFO_COLORS: infopanel::OnColors((infopanel::InfoMsg*)l); return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (ctrl) {
            if (w == 'O') Command(ID_OPEN); else if (w == 'A') Command(ID_SELALL); else if (w == 'C') Command(ID_COPY); else if (w == VK_OEM_COMMA) Command(ID_SETTINGS);
            return 0;
        }
        switch (w) {
        case 'P': Command(ID_PIXEL); break;
        case 'S': if (!(l & (1 << 30))) Command(ID_SELMODE); break;
        case VK_SPACE: if (!(l & (1 << 30))) Command(ID_PLAY); break;
        case 'Q': Command(ID_FPREV); break;
        case 'E': Command(ID_FNEXT); break;
        case 'I': Command(ID_INFO); break;
        case 'R': Command(ID_RESET); break;
        case 'B': Command(ID_BGCYCLE); break;
        case VK_ESCAPE: Command(ID_DESEL); break;
        case VK_DELETE: if (!(l & (1 << 30))) Command(ID_DELETE); break;   // 押しっぱなしの繰り返しは無視（Space / S と同じ）
        case VK_RIGHT: NavigateRel(1); break;
        case VK_LEFT: NavigateRel(-1); break;
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TM_FLASH) { KillTimer(h, TM_FLASH); g_flash.clear(); RefreshStatus(); }
        else if (w == TM_DIR) { KillTimer(h, TM_DIR); OnDirChanged(); }
        else if (w == TM_RETRY) { KillTimer(h, TM_RETRY); OnDirChanged(); }
        return 0;
    case WM_DIRCHANGED: SetTimer(h, TM_DIR, 200, nullptr); return 0;
    case WM_DROPFILES: OnFilesDropped((HDROP)w); return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        // ホイールはカーソルの下の側で働く（パネルの上ならテキストのスクロール）
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (g_infoOn) { RECT r; GetWindowRect(infopanel::Hwnd(), &r); if (PtInRect(&r, p)) return SendMessageW(infopanel::Hwnd(), m, w, l); }
        return SendMessageW(viewer::Hwnd(), m, w, l);
    }
    case WM_CLOSE: SaveSettings(); DestroyWindow(h); return 0;
    case WM_DESTROY: infopanel::Shutdown(); StopWatch(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_BAR_CLASSES | ICC_WIN95_CLASSES }; InitCommonControlsEx(&ic);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) return 1;

    settings::Data cfg;
    settings::Load(cfg);
    lang::Set(cfg.lang);   // 書体を決める前に言語を決める
    ui::Init();
    g_infoOn = cfg.infoOn;
    g_infoW = std::max(kInfoMin, cfg.infoW);

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"TLIVMain";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    g_hwnd = nullptr;
    HWND hw = CreateWindowExW(WS_EX_ACCEPTFILES, L"TLIVMain", L"TLIV", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, cfg.x, cfg.y, cfg.w, cfg.h, nullptr, nullptr, hi, nullptr);
    if (!hw) return 1;
    viewer::SetCustom(cfg.custom); viewer::SetBg(cfg.bg); viewer::SetPixel(cfg.pixel);
    ShowWindow(hw, cfg.maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hw);
    RefreshStatus();

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) OpenPath(argv[1]);
    if (argv) LocalFree(argv);

    // 描き直しが要るときだけスワップチェーンの待機オブジェクトを待ち、垂直同期1回に1回だけ描く。
    // キューにメッセージが残っていても待ちっぱなしにしない（MWMO_INPUTAVAILABLE）。
    // メッセージを1件処理するたびに、描き直しか次のコマの時刻が来ていれば抜ける
    // （マウス移動が途切れずに届き続けても、コマ送りと描画が止まらない）
    MSG msg;
    for (;;) {
        viewer::LoopAlive();
        viewer::AnimTick();   // アニメのコマ送り（絶対時刻）。待つ前に呼ぶ：進んだら今回の待ちで画面切り替えを待てる
        HANDLE hw2 = viewer::NeedsFrame() ? viewer::FrameHandle() : nullptr;
        DWORD r = MsgWaitForMultipleObjectsEx(hw2 ? 1 : 0, hw2 ? &hw2 : nullptr, viewer::AnimTimeout(), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        viewer::LoopAlive();
        // 入力が届いていると、待ちはそちらを先に返して合図を見せないことがあるので、合図は別に確かめる
        if (hw2 && (r == WAIT_OBJECT_0 || WaitForSingleObject(hw2, 0) == WAIT_OBJECT_0)) viewer::RenderIfDirty();
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return (int)msg.wParam;
            viewer::LoopAlive();
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (viewer::NeedsFrame() || viewer::AnimTimeout() == 0) break;
        }
    }
}
