#include "ui.h"
#include "decode.h"
#include "viewer.h"
#include "lang.h"
#include <algorithm>
#include <iterator>

HFONT g_font = nullptr;
int g_scale = 1;   // 実際の拡大率。バー類は1倍で描いて main が整数倍に転送する

namespace ui {

// 立体枠の5色
static const COLORREF FACE = RGB(0xc0, 0xc0, 0xc0), HI = RGB(255, 255, 255), LT = RGB(0xdf, 0xdf, 0xdf), SH = RGB(0x80, 0x80, 0x80), DK = RGB(0, 0, 0);
static const COLORREF TXT = RGB(0, 0, 0), PIXEL_BLUE = RGB(0, 0, 0x80);

struct Tool { const char* icon; int cmd; lang::Id tip; };   // icon == nullptr は区切り
static const Tool kTools[] = {
    { "prev", ID_PREV, lang::TIP_PREV }, { "next", ID_NEXT, lang::TIP_NEXT }, { nullptr, 0, lang::kIdCount },
    { "fback", ID_FPREV, lang::TIP_FPREV }, { "play", ID_PLAY, lang::TIP_PLAY }, { "fnext", ID_FNEXT, lang::TIP_FNEXT }, { nullptr, 0, lang::kIdCount },
    { "zoomin", ID_ZOOMIN, lang::TIP_ZOOMIN }, { "zoomout", ID_ZOOMOUT, lang::TIP_ZOOMOUT }, { "reset", ID_RESET, lang::TIP_RESET }, { nullptr, 0, lang::kIdCount },
    { "pixel", ID_PIXEL, lang::TIP_PIXEL }, { "bg", ID_BGCYCLE, lang::TIP_BG }, { nullptr, 0, lang::kIdCount },
    { "select", ID_SELMODE, lang::TIP_SELECT }, { "copy", ID_COPY, lang::TIP_COPY }, { nullptr, 0, lang::kIdCount },
    { "info", ID_INFO, lang::TIP_INFO }, { nullptr, 0, lang::kIdCount },
    { "delete", ID_DELETE, lang::TIP_DELETE },
};
static const lang::Id kMenus[] = { lang::M_FILE, lang::M_EDIT, lang::M_VIEW, lang::M_HELP };
static const int kNumMenus = (int)std::size(kMenus);

struct Icon { std::vector<uint32_t> px; };
static const char* kIconNames[] = { "prev", "next", "zoomin", "zoomout", "reset", "pixel", "bg", "copy", "delete", "info", "fback", "play", "fnext", "select" };
static Icon g_icons[std::size(kIconNames)];
static bool g_panelOn = false;
static int g_panelW = 0;
static int g_textH = 12;

static Icon* FindIcon(const char* n) {
    for (size_t i = 0; i < std::size(kIconNames); i++) if (!strcmp(kIconNames[i], n)) return &g_icons[i];
    return nullptr;
}

void Init() {
    for (size_t i = 0; i < std::size(kIconNames); i++) {
        wchar_t name[32]; swprintf_s(name, L"ICON_%S", kIconNames[i]);
        for (wchar_t* c = name; *c; c++) *c = towupper(*c);
        HRSRC r = FindResourceW(nullptr, name, RT_RCDATA);
        if (!r) continue;
        HGLOBAL g = LoadResource(nullptr, r);
        int w, h;
        DecodePngResource(LockResource(g), SizeofResource(nullptr, r), w, h, g_icons[i].px);
    }
    RebuildFont();
}

static HFONT MakeFont(const wchar_t* face, int h) {
    return CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FIXED_PITCH | FF_DONTCARE, face);
}

// 今の言語の書体を決める。候補を先頭から試し、実際に選ばれた書体名が候補と一致したものを使う（最後の候補は必ず使う）
static wchar_t g_face[LF_FACESIZE] = L"MS UI Gothic";
static int g_facePx = 12;
static void ResolveFace() {
    int n; const lang::FaceCand* c = lang::FaceCands(lang::Get(), n);
    HDC dc = GetDC(nullptr);
    for (int i = 0; i < n; i++) {
        bool ok = i == n - 1;
        if (!ok) {
            HFONT f = MakeFont(c[i].face, -c[i].px);
            HGDIOBJ o = SelectObject(dc, f);
            wchar_t got[LF_FACESIZE] = L""; GetTextFaceW(dc, LF_FACESIZE, got);
            SelectObject(dc, o); DeleteObject(f);
            ok = _wcsicmp(got, c[i].face) == 0;
        }
        if (ok) { wcscpy_s(g_face, c[i].face); g_facePx = c[i].px; break; }
    }
    ReleaseDC(nullptr, dc);
}
const wchar_t* FaceName() { return g_face; }
int FacePx() { return g_facePx; }

// 今の言語の UI フォントを scale 倍で作る
HFONT CreateUiFont(int scale) { return MakeFont(g_face, -g_facePx * scale); }

// UI のバー類の文字は常に1倍（拡大は転送時）。言語が変わったときも呼ぶ
void RebuildFont() {
    ResolveFace();
    if (g_font) DeleteObject(g_font);
    g_font = CreateUiFont(1);
    HDC dc = GetDC(nullptr);
    HGDIOBJ o = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    g_textH = tm.tmHeight;
    SelectObject(dc, o); ReleaseDC(nullptr, dc);
}

void SetPanel(bool on, int w) { g_panelOn = on; g_panelW = on ? w : 0; }

// ---------------------------------------------------------------- 部品
static void Fill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    RECT r = { x, y, x + w, y + h };
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}
// 太さ 1px の枠。左上 tl 色、右下 br 色
static void Ring(HDC dc, int x, int y, int w, int h, COLORREF tl, COLORREF br) {
    Fill(dc, x, y + h - 1, w, 1, br); Fill(dc, x + w - 1, y, 1, h, br);
    Fill(dc, x, y, w, 1, tl); Fill(dc, x, y, 1, h, tl);
}
static void Text(HDC dc, const std::wstring& s, const RECT& box, COLORREF c, bool right = false) {
    if (s.empty()) return;
    SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int x = right ? box.right - sz.cx : box.left;
    int y = box.top + ((box.bottom - box.top) - g_textH) / 2;
    ExtTextOutW(dc, x, y, ETO_CLIPPED, &box, s.c_str(), (UINT)s.size(), nullptr);
}
static int TextW(HDC dc, const std::wstring& s) { SIZE sz = {}; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx; }

// disabled：不透明な画素ごとに明るさ L を出し、灰色 0x80 + L*63/255 で描く（形と模様は残す）
static void DrawIcon(HDC dc, const Icon* ic, int x, int y, bool disabled = false) {
    if (!ic || ic->px.size() != 256) return;
    if (disabled) {
        for (int py = 0; py < 16; py++) for (int px = 0; px < 16; px++) {
            uint32_t v = ic->px[py * 16 + px];
            if ((v >> 24) != 255) continue;
            int L = (int)(((v >> 16) & 255) * 299 + ((v >> 8) & 255) * 587 + (v & 255) * 114) / 1000;
            int g = 0x80 + L * 63 / 255;
            Fill(dc, x + px, y + py, 1, 1, RGB(g, g, g));
        }
        return;
    }
    for (int py = 0; py < 16; py++) for (int px = 0; px < 16; px++) {
        uint32_t v = ic->px[py * 16 + px];
        if ((v >> 24) == 255) Fill(dc, x + px, y + py, 1, 1, RGB((v >> 16) & 255, (v >> 8) & 255, v & 255));
    }
}

// ---------------------------------------------------------------- 配置
Layout Calc(int cw, int ch) {
    int pad = 2;
    Layout L;
    L.menu = { pad, pad, cw - pad, pad + 18 };
    L.toolbar = { pad, L.menu.bottom, cw - pad, L.menu.bottom + 26 };
    L.status = { pad, ch - pad - 20, cw - pad, ch - pad };
    L.wrap = { pad, L.toolbar.bottom, cw - pad - g_panelW, L.status.top - 2 };
    L.splitter = L.panel = RECT{ 0, 0, 0, 0 };
    if (g_panelOn) {
        L.splitter = { L.wrap.right, L.wrap.top, L.wrap.right + 4, L.wrap.bottom };
        L.panel = { L.splitter.right, L.wrap.top, cw - pad, L.wrap.bottom };
    }
    L.view = { L.wrap.left + 4, L.wrap.top + 4, L.wrap.right - 4, L.wrap.bottom - 4 };
    return L;
}

static int g_cw = 0, g_ch = 0;
void SetSize(int cw, int ch) { g_cw = cw; g_ch = ch; }

static int MenuTextW(int i) {
    HDC dc = GetDC(nullptr); HGDIOBJ o = SelectObject(dc, g_font);
    int w = TextW(dc, lang::T(kMenus[i]));
    SelectObject(dc, o); ReleaseDC(nullptr, dc);
    return w;
}
RECT MenuItemRect(int i) {
    int x = 2;
    for (int k = 0; k < i; k++) x += MenuTextW(k) + 12;
    return RECT{ x, 2, x + MenuTextW(i) + 12, 2 + 18 };
}
int HitMenu(int x, int y) {
    POINT p = { x, y };
    for (int i = 0; i < kNumMenus; i++) { RECT r = MenuItemRect(i); if (PtInRect(&r, p)) return i; }
    return -1;
}

int ToolCount() { int n = 0; for (auto& t : kTools) if (t.icon) n++; return n; }
static RECT ToolRectAt(int toolbarTop, int idx) {
    int x = 4, n = 0;
    for (auto& t : kTools) {
        if (!t.icon) { x += 8; continue; }
        if (n == idx) return RECT{ x, toolbarTop + 2, x + 24, toolbarTop + 24 };
        x += 24; n++;
    }
    return RECT{ 0, 0, 0, 0 };
}
RECT ToolRect(int i) { return ToolRectAt(Calc(g_cw, g_ch).toolbar.top, i); }
int HitTool(int x, int y) { POINT p = { x, y }; for (int i = 0; i < ToolCount(); i++) { RECT r = ToolRect(i); if (PtInRect(&r, p)) return i; } return -1; }
static const Tool& NthTool(int i) { int n = 0; for (auto& t : kTools) if (t.icon) { if (n == i) return t; n++; } return kTools[0]; }
int ToolCmd(int i) { return NthTool(i).cmd; }
bool ToolEnabled(int i, bool animated) { int c = NthTool(i).cmd; return animated || (c != ID_PLAY && c != ID_FPREV && c != ID_FNEXT); }
int MinClientWidth() {
    int x = 4;
    for (auto& t : kTools) x += t.icon ? 24 : 8;
    return x + 4 + 2;   // 右に余白4と外枠2
}
const wchar_t* ToolTip(int i) { return lang::T(NthTool(i).tip); }
RECT StatusRect() { Layout L = Calc(g_cw, g_ch); return L.status; }

HMENU BuildMenu(int idx) {
    HMENU m = CreatePopupMenu();
    using namespace lang;
    // 項目名（& つき）に、必要ならキー（	 以降）を足す。キーの表記は全言語で同じ
    auto lab = [](Id id, const wchar_t* key) { std::wstring t = T(id); if (key) { t += L'	'; t += key; } return t; };
    auto add = [&](UINT id, Id name, const wchar_t* key, UINT flags = MF_STRING) { AppendMenuW(m, flags, id, lab(name, key).c_str()); };
    switch (idx) {
    case 0: add(ID_OPEN, P_OPEN, L"Ctrl+O"); add(ID_SHOWEXP, P_SHOWEXP, nullptr); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            add(ID_SETTINGS, P_SETTINGS, L"Ctrl+,"); add(ID_EXIT, P_EXIT, nullptr); break;
    case 1: add(ID_COPY, P_COPY, L"Ctrl+C");
            add(ID_SELMODE, P_SELMODE, L"S", MF_STRING | (viewer::SelectMode() ? MF_CHECKED : MF_UNCHECKED));
            add(ID_SELALL, P_SELALL, L"Ctrl+A"); add(ID_DESEL, P_DESEL, L"Esc"); add(ID_DELETE, P_DELETE, L"Del"); break;
    case 2: add(ID_ZOOMIN, P_ZOOMIN, nullptr); add(ID_ZOOMOUT, P_ZOOMOUT, nullptr); add(ID_RESET, P_RESET, L"R");
            add(ID_INFO, P_INFO, L"I", MF_STRING | (g_panelOn ? MF_CHECKED : MF_UNCHECKED));
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            { UINT g = MF_STRING | (viewer::Animated() ? MF_ENABLED : MF_GRAYED);
              add(ID_PLAY, P_PLAY, L"Space", g);
              add(ID_FPREV, P_FPREV, L"Q", g);
              add(ID_FNEXT, P_FNEXT, L"E", g); } break;
    case 3: add(ID_KEYS, P_KEYS, nullptr); add(ID_ABOUT, P_ABOUT, nullptr); break;
    }
    return m;
}

// ---------------------------------------------------------------- 描画
void Paint(HDC dc, int cw, int ch, const UiState& st) {
    Layout L = Calc(cw, ch);
    HGDIOBJ oldf = SelectObject(dc, g_font);
    Fill(dc, 0, 0, cw, ch, FACE);

    // メニューバー
    for (int i = 0; i < kNumMenus; i++) {
        RECT r = MenuItemRect(i);
        if (st.openMenu == i) { Ring(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, SH, HI); }
        else if (st.hoverMenu == i && st.openMenu < 0) { Ring(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, HI, SH); }
        int off = st.openMenu == i ? 1 : 0;
        RECT tr = { r.left + 6 + off, r.top + off, r.right - 6, r.bottom };
        const std::wstring label = lang::T(kMenus[i]);
        Text(dc, label, tr, TXT);
        // 下線：「ファイル(F)」のように括弧つきなら括弧の中の英字、そうでなければ頭文字
        size_t ui = 0;
        if (label.size() >= 3 && label.back() == L')' && label[label.size() - 3] == L'(') ui = label.size() - 2;
        int ux = tr.left + (ui ? TextW(dc, label.substr(0, ui)) : 0), cwid = TextW(dc, label.substr(ui, 1));
        int ty = tr.top + ((tr.bottom - tr.top) - g_textH) / 2 + g_textH;
        Fill(dc, ux, ty - 1, cwid, 1, TXT);
    }

    // ツールバー
    int tt = L.toolbar.top;
    Fill(dc, L.toolbar.left, tt, L.toolbar.right - L.toolbar.left, 1, SH);
    Fill(dc, L.toolbar.left, tt + 1, L.toolbar.right - L.toolbar.left, 1, HI);
    {
        int x = 4, n = 0;
        for (auto& t : kTools) {
            if (!t.icon) {
                int sy = tt + 2 + 5 / 2;
                Fill(dc, x + 3, sy, 1, 18, SH); Fill(dc, x + 4, sy, 1, 18, HI);
                x += 8; continue;
            }
            RECT r = ToolRectAt(tt, n);
            bool en = ToolEnabled(n, st.animated);
            bool on = en && ((t.cmd == ID_PIXEL && st.pixelOn) || (t.cmd == ID_INFO && st.infoOn) || (t.cmd == ID_SELMODE && st.selectOn) || (t.cmd == ID_PLAY && st.playOn));
            bool press = en && st.pressTool == n && st.hoverTool == n;
            int w = 24, h = 22;
            if (on || press) {
                if (on) Fill(dc, r.left, r.top, w, h, LT);
                Ring(dc, r.left, r.top, w, h, SH, HI);
            } else if (en && st.hoverTool == n) Ring(dc, r.left, r.top, w, h, HI, SH);
            DrawIcon(dc, FindIcon(t.icon), r.left + 4, r.top + 3, !en);
            x += 24; n++;
        }
    }

    // 表示領域の枠
    {
        const RECT& w = L.wrap; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
    }

    // 情報パネルの仕切りと枠（中身の EDIT は main が重ねる）
    if (g_panelOn) {
        const RECT& sp = L.splitter; Ring(dc, sp.left, sp.top, sp.right - sp.left, sp.bottom - sp.top, HI, SH);
        const RECT& w = L.panel; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
    }

    // ステータスバー
    {
        const RECT& sr = L.status; int top = sr.top, h = 20, gap = 2;
        const StatusInfo& si = st.status;
        struct P { std::wstring t; int w; bool right; COLORREF col; bool sw; };
        std::vector<P> ps;   // 右側の欄を左から順に
        if (si.basic) {
            ps.push_back({ si.size, TextW(dc, si.size) + 10, false, TXT, false });
            if (!si.frame.empty()) ps.push_back({ si.frame, TextW(dc, si.frame) + 10, false, TXT, false });
            // 欄の幅は文字の幅から決める（言語が変わっても切れない）。下の数字は英語のときの幅で、それ以下にはしない
            int zoomW = std::max(52, TextW(dc, L"6400.0%") + 10);
            int modeW = std::max(48, std::max(TextW(dc, lang::T(lang::ST_PIXEL)), TextW(dc, lang::T(lang::ST_NORMAL))) + 10);
            int posW = std::max(76, TextW(dc, L"9999, 9999") + 10);
            int colW = std::max(100, std::max(TextW(dc, L"#FFFFFF a255"), TextW(dc, lang::T(lang::ST_TRANSPARENT))) + 13 + 10);
            ps.push_back({ si.zoom, zoomW, true, TXT, false });
            ps.push_back({ si.mode, modeW, false, si.pixel ? PIXEL_BLUE : TXT, false });
            if (si.pixel) { ps.push_back({ si.pos, posW, false, TXT, false }); ps.push_back({ si.color, colW, false, TXT, true }); }
        }
        // 番号欄
        int idxW = TextW(dc, si.index) + 10;
        int rightTotal = idxW;
        for (auto& p : ps) rightTotal += p.w + gap;
        int nameW = (sr.right - sr.left) - rightTotal - gap;
        if (nameW < 0) nameW = 0;
        int x = sr.left;
        auto pane = [&](int w, const std::wstring& t, bool right, COLORREF col, bool swatch) {
            Ring(dc, x, top, w, h, SH, HI);
            RECT tb = { x + 5, top + 2, x + w - 5, top + h - 2 };
            if (swatch && si.hasSwatch) {
                int sz = 10;
                Fill(dc, tb.left, top + 5, sz, sz, DK); Fill(dc, tb.left + 1, top + 6, sz - 2, sz - 2, si.swatch);
                tb.left += sz + 3;
            }
            Text(dc, t, tb, col, right);
            x += w + gap;
        };
        pane(nameW, si.name, false, TXT, false);
        pane(idxW, si.index, false, TXT, false);
        for (auto& p : ps) pane(p.w, p.t, p.right, p.col, p.sw);
    }
    SelectObject(dc, oldf);
}

}  // namespace ui
