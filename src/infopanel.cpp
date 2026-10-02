#include "infopanel.h"
#include "ui.h"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace infopanel {

static HWND g_edit = nullptr, g_parent = nullptr;
static HFONT g_pfont = nullptr;
static WNDPROC g_oldProc = nullptr;
static unsigned g_serial = 0;
static std::shared_ptr<std::atomic<bool>> g_cancel;
static int g_colOff = -1, g_colLen = 0;
static int g_id = 0;
static bool g_noWrap = false;                 // 折り返さない EDIT（横スクロール）に替えているか
static int g_px = 0, g_py = 0, g_pw = 10, g_ph = 10;   // 今の置き場所（EDIT を作り直すときに引き継ぐ）
static bool g_shown = false;

HWND Hwnd() { return g_edit; }

// テキストにフォーカスがある間は、Delete や単キーを画像の操作にしない（ここで止める）。
// ホイールはカーソルの下の側で働かせる
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { SetFocus(g_parent); return 0; }
        if (w == VK_DELETE) return 0;
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'A') { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
        break;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) }; RECT r; GetWindowRect(h, &r);
        if (!PtInRect(&r, p)) return SendMessageW(g_parent, m, w, l);   // 画像の上なら画像の操作
        break;
    }
    }
    return CallWindowProcW(g_oldProc, h, m, w, l);
}

static HWND MakeEdit(bool noWrap) {
    DWORD st = WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (noWrap) st |= WS_HSCROLL | ES_AUTOHSCROLL;
    HWND e = CreateWindowExW(0, L"EDIT", L"", st, g_px, g_py, g_pw, g_ph, g_parent, (HMENU)(INT_PTR)g_id, GetModuleHandleW(nullptr), nullptr);
    if (!e) return nullptr;
    SendMessageW(e, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    g_oldProc = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditProc);
    if (g_pfont) SendMessageW(e, WM_SETFONT, (WPARAM)g_pfont, TRUE);
    return e;
}

bool Create(HWND parent, int id) {
    g_parent = parent; g_id = id;
    g_edit = MakeEdit(false);
    if (!g_edit) return false;
    SetFont(g_scale);
    return true;
}

// 折り返す EDIT と折り返さない EDIT を入れ替える（折り返しの計算は文字数に比例して遅く、非常に大きい中身では固まるため）
static void SetNoWrap(bool noWrap) {
    if (noWrap == g_noWrap || !g_edit) return;
    HWND ne = MakeEdit(noWrap);
    if (!ne) return;
    bool focus = GetFocus() == g_edit;
    HWND old = g_edit;
    g_edit = ne; g_noWrap = noWrap;
    if (g_shown) ShowWindow(ne, SW_SHOWNA);
    DestroyWindow(old);
    if (focus) SetFocus(ne);
}

void SetFont(int scale) {
    HFONT nf = ui::CreateUiFont(scale);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, (WPARAM)nf, TRUE);
    if (g_pfont) DeleteObject(g_pfont);
    g_pfont = nf;
}

void Place(int x, int y, int w, int h) {
    g_px = x; g_py = y; g_pw = w; g_ph = h;
    if (g_edit) SetWindowPos(g_edit, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}
void Show(bool on) { g_shown = on; if (g_edit) ShowWindow(g_edit, on ? SW_SHOWNA : SW_HIDE); }

static void SetTextTop(const std::wstring& t) {
    SetWindowTextW(g_edit, t.c_str());
    SendMessageW(g_edit, EM_SETSEL, 0, 0);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(g_edit, EM_LINESCROLL, 0, -0x7FFFFF);
}

void Clear() {
    g_serial++;
    if (g_cancel) g_cancel->store(true);
    g_colOff = -1;
    if (g_edit) SetTextTop(L"");
}

// 裏の作業（ファイルの読み込み・解析・色数の集計）は常に1本のワーカーだけが行う。
// 注文の受け口は1つ（mutex + condition_variable）で、新しい注文は待っている注文を上書きする（最新だけが残る）。
// 動いている作業には止めの合図（cancel）を送る。ワーカーは detach し、共有状態は shared_ptr で持つ（終了時に待たない）
namespace {
struct Order { InfoReq rq; unsigned serial = 0; std::shared_ptr<std::atomic<bool>> cancel; HWND parent = nullptr; };
struct Hub {
    std::mutex m; std::condition_variable cv;
    bool has = false, quit = false;
    Order ord;
};
std::shared_ptr<Hub> g_hub;

void Worker(std::shared_ptr<Hub> hub) {
    for (;;) {
        Order o;
        {
            std::unique_lock<std::mutex> lk(hub->m);
            hub->cv.wait(lk, [&] { return hub->has || hub->quit; });
            if (hub->quit) return;
            o = std::move(hub->ord); hub->has = false;
        }
        if (o.cancel->load()) continue;   // もう古い注文
        unsigned serial = o.serial; HWND parent = o.parent;
        RunInfoJob(o.rq, o.cancel,
            [=](InfoResult&& r) { auto* m = new InfoMsg; m->serial = serial; m->res = std::move(r); if (!PostMessageW(parent, WM_INFO_TEXT, 0, (LPARAM)m)) delete m; },
            [=](std::wstring&& c) { auto* m = new InfoMsg; m->serial = serial; m->colors = std::move(c); if (!PostMessageW(parent, WM_INFO_COLORS, 0, (LPARAM)m)) delete m; });
    }
}
}  // namespace

void Request(const InfoReq& rq) {
    Clear();
    g_cancel = std::make_shared<std::atomic<bool>>(false);
    try {
        if (!g_hub) {
            g_hub = std::make_shared<Hub>();
            try { std::thread(Worker, g_hub).detach(); }
            catch (...) { g_hub.reset(); return; }   // スレッドを作れなければ、パネルは空のまま
        }
        std::lock_guard<std::mutex> lk(g_hub->m);
        g_hub->ord.rq = rq; g_hub->ord.serial = g_serial; g_hub->ord.cancel = g_cancel; g_hub->ord.parent = g_parent;
        g_hub->has = true;
        g_hub->cv.notify_one();
    } catch (...) {}   // 注文を作れなかった（メモリ不足）。パネルは空のまま
}

void Shutdown() {
    if (g_cancel) g_cancel->store(true);
    if (!g_hub) return;
    { std::lock_guard<std::mutex> lk(g_hub->m); g_hub->quit = true; }
    g_hub->cv.notify_one();
    g_hub.reset();   // ワーカーは自分の shared_ptr で状態を持っている。待たない
}

void OnText(InfoMsg* m) {
    std::unique_ptr<InfoMsg> own(m);
    if (m->serial != g_serial || !g_edit) return;
    g_colOff = m->res.colorsOff; g_colLen = m->res.colorsLen;
    SetNoWrap(m->res.noWrap);
    SetTextTop(m->res.text);
}

// 色数が数え終わった。"counting..." の部分だけ置き換え、選択とスクロールは保つ
void OnColors(InfoMsg* m) {
    std::unique_ptr<InfoMsg> own(m);
    if (m->serial != g_serial || !g_edit || g_colOff < 0) return;
    DWORD a = 0, b = 0; SendMessageW(g_edit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_edit, EM_SETREADONLY, FALSE, 0);
    SendMessageW(g_edit, EM_SETSEL, g_colOff, g_colOff + g_colLen);
    SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)m->colors.c_str());
    SendMessageW(g_edit, EM_SETREADONLY, TRUE, 0);
    int delta = (int)m->colors.size() - g_colLen, end = g_colOff + g_colLen;
    auto adj = [&](DWORD p) { return (DWORD)((int)p >= end ? (int)p + delta : (int)p); };
    SendMessageW(g_edit, EM_SETSEL, adj(a), adj(b));
    int now = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (now != first) SendMessageW(g_edit, EM_LINESCROLL, 0, first - now);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, nullptr, TRUE);
    g_colOff = -1;
}

}  // namespace infopanel
