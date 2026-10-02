#include "viewer.h"
#include "lang.h"
#include "util.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_3.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace viewer {

static const float kLevels[] = { 1.f / 8, 1.f / 4, 1.f / 3, 1.f / 2, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32 };
static const int kNumLevels = sizeof kLevels / sizeof *kLevels;

// プリマルチ BGRA のビットマップの性質（96dpi）
static D2D1_BITMAP_PROPERTIES1 PbgraProps(D2D1_BITMAP_OPTIONS opt = D2D1_BITMAP_OPTIONS_NONE) {
    return D2D1::BitmapProperties1(opt, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
}

struct State {
    HWND hwnd = nullptr, parent = nullptr;
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGISwapChain1> sc;
    ComPtr<IDXGISwapChain2> sc2;
    HANDLE frameWait = nullptr;      // 最大フレーム遅延1の待機オブジェクト（スワップチェーンが所有）
    ComPtr<ID2D1Factory1> fac;
    ComPtr<ID2D1Device> dev;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1DeviceContext5> dc5;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1Bitmap> bmp, textBmp;
    ComPtr<ID2D1BitmapBrush1> checker;
    ComPtr<ID2D1SvgDocument> svg;
    // 縮小キャッシュ（通常モードで倍率<1のとき）
    ComPtr<ID2D1Bitmap1> cache; int cacheW = 0, cacheH = 0; unsigned cacheSerial = 0;
    unsigned serial = 0;             // 画像が入れ替わるたびに増やす
    ComPtr<ID2D1SolidColorBrush> bgBrush, whiteBrush, blackBrush;
    ComPtr<ID2D1StrokeStyle> antsStyle[8];
    bool dirty = false;              // 描き直しが要る（描画は垂直同期1回に1回）
    int W = 1, H = 1;

    std::unique_ptr<Image> img;
    bool failed = false;
    int frame = 0;
    bool pixel = true;
    int bg = 0;
    COLORREF custom = RGB(0x5a, 0x7a, 0x8a);
    float zoom = 1, ox = 0, oy = 0;
    bool hasSel = false; int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
    bool mouseIn = false; int mx = 0, my = 0;
    bool pressing = false, panning = false; int pmx = 0, pmy = 0; float pox = 0, poy = 0;   // pressing: 左ボタンを押している（動きが閾値を超えたら panning）
    bool selectMode = false;
    bool selDrag = false; int ax = 0, ay = 0;
    int ants = 0;
    // アニメ再生：コマの表示開始時刻を QPC の絶対時刻で持つ
    bool playing = false, timerRes = false;
    long long animStart = 0;         // 今のコマの表示開始時刻（QPC）
    int wheelAcc = 0, hwheelAcc = 0;
} V;

static const UINT_PTR TM_ANTS = 2;

// ---------------------------------------------------------------- デバイス
static bool CreateSwapAndTarget() {
    V.dc->SetTarget(nullptr);
    V.target.Reset();
    if (!V.sc) {
        ComPtr<IDXGIDevice> dxgi; V.d3d.As(&dxgi);
        ComPtr<IDXGIAdapter> ad; ComPtr<IDXGIFactory2> f2;
        if (FAILED(dxgi->GetAdapter(&ad)) || FAILED(ad->GetParent(IID_PPV_ARGS(&f2)))) return false;
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = V.W; sd.Height = V.H; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (FAILED(f2->CreateSwapChainForHwnd(V.d3d.Get(), V.hwnd, &sd, nullptr, nullptr, &V.sc))) return false;
    } else if (FAILED(V.sc->ResizeBuffers(0, V.W, V.H, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) return false;
    V.sc2.Reset(); V.frameWait = nullptr;
    if (SUCCEEDED(V.sc.As(&V.sc2))) { V.sc2->SetMaximumFrameLatency(1); V.frameWait = V.sc2->GetFrameLatencyWaitableObject(); }
    ComPtr<IDXGISurface> surf;
    if (FAILED(V.sc->GetBuffer(0, IID_PPV_ARGS(&surf)))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
    if (FAILED(V.dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &V.target))) return false;
    V.dc->SetTarget(V.target.Get());
    V.dc->SetDpi(96, 96);
    return true;
}

static bool InitDevices() {
    UINT fl = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, fl, nullptr, 0, D3D11_SDK_VERSION, &V.d3d, nullptr, nullptr);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, fl, nullptr, 0, D3D11_SDK_VERSION, &V.d3d, nullptr, nullptr);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice> dxgi; V.d3d.As(&dxgi);
    if (!V.fac && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void**)V.fac.GetAddressOf()))) return false;
    if (FAILED(V.fac->CreateDevice(dxgi.Get(), &V.dev))) return false;
    if (FAILED(V.dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &V.dc))) return false;
    V.dc.As(&V.dc5);
    return CreateSwapAndTarget();
}

static void ReleaseDevices() {
    V.dc->SetTarget(nullptr);
    V.bmp.Reset(); V.textBmp.Reset(); V.checker.Reset(); V.svg.Reset(); V.target.Reset(); V.cache.Reset();
    V.bgBrush.Reset(); V.whiteBrush.Reset(); V.blackBrush.Reset(); for (auto& a : V.antsStyle) a.Reset();
    V.sc2.Reset(); V.frameWait = nullptr; V.sc.Reset(); V.dc5.Reset(); V.dc.Reset(); V.dev.Reset(); V.d3d.Reset();
}

static bool MakeImageBitmap() {
    V.bmp.Reset();
    if (!V.img || V.img->svg) return true;
    const Image& im = *V.img;
    ComPtr<ID2D1Bitmap1> b;
    if (FAILED(V.dc->CreateBitmap(D2D1::SizeU(im.w, im.h), im.frames[V.frame].px.data(), im.w * 4, PbgraProps(), &b))) return false;
    V.bmp = b;
    return true;
}

// ---------------------------------------------------------------- SVG
static bool SvgSizeFromDoc(ID2D1SvgDocument* doc, float& w, float& h) {
    ComPtr<ID2D1SvgElement> root; doc->GetRoot(&root);
    if (!root) return false;
    w = h = 0;
    D2D1_SVG_LENGTH l;
    if (SUCCEEDED(root->GetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &l, sizeof l)) && l.units == D2D1_SVG_LENGTH_UNITS_NUMBER) w = l.value;
    if (SUCCEEDED(root->GetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &l, sizeof l)) && l.units == D2D1_SVG_LENGTH_UNITS_NUMBER) h = l.value;
    D2D1_SVG_VIEWBOX vb;
    if ((w <= 0 || h <= 0) && SUCCEEDED(root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &vb, sizeof vb)) && vb.width > 0 && vb.height > 0) {
        if (w <= 0 && h > 0) w = h * vb.width / vb.height;
        else if (h <= 0 && w > 0) h = w * vb.height / vb.width;
        else { w = vb.width; h = vb.height; }
    }
    if (w <= 0 || h <= 0) { w = 256; h = 256; }
    return true;
}

// SVG のデータから文書を作る（viewport は後から合わせる）。失敗したら null
static ComPtr<ID2D1SvgDocument> MakeSvgDoc(const Image& im, float w, float h) {
    ComPtr<ID2D1SvgDocument> doc;
    if (!V.dc5) return doc;
    ComPtr<IStream> s = MemStream(im.svgData.data(), im.svgData.size());
    if (!s || FAILED(V.dc5->CreateSvgDocument(s.Get(), D2D1::SizeF(w, h), &doc))) doc.Reset();
    return doc;
}

static bool PrepareSvg(Image& im, ComPtr<ID2D1SvgDocument>& doc) {
    doc = MakeSvgDoc(im, 256, 256);
    if (!doc) return false;
    float w, h;
    if (!SvgSizeFromDoc(doc.Get(), w, h)) return false;
    int iw = std::max(1, (int)std::lround(w)), ih = std::max(1, (int)std::lround(h));
    UINT maxb = V.dc->GetMaximumBitmapSize();
    if ((UINT)iw > maxb || (UINT)ih > maxb) return false;
    doc->SetViewportSize(D2D1::SizeF((float)iw, (float)ih));
    im.w = iw; im.h = ih;

    // 1倍でラスタライズして、色の取得とコピーに使う
    ComPtr<ID2D1Bitmap1> tgt, cpu;
    if (FAILED(V.dc->CreateBitmap(D2D1::SizeU(iw, ih), nullptr, 0, PbgraProps(D2D1_BITMAP_OPTIONS_TARGET), &tgt)) ||
        FAILED(V.dc->CreateBitmap(D2D1::SizeU(iw, ih), nullptr, 0, PbgraProps(D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ), &cpu))) return false;
    V.dc->SetTarget(tgt.Get());
    V.dc->BeginDraw();
    V.dc->SetTransform(D2D1::Matrix3x2F::Identity());
    V.dc->Clear(D2D1::ColorF(0, 0));
    V.dc5->DrawSvgDocument(doc.Get());
    HRESULT hr = V.dc->EndDraw();
    V.dc->SetTarget(V.target.Get());
    if (FAILED(hr) || FAILED(cpu->CopyFromBitmap(nullptr, tgt.Get(), nullptr))) return false;
    D2D1_MAPPED_RECT m;
    if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return false;
    Frame f; f.px.resize((size_t)iw * ih);
    for (int y = 0; y < ih; y++) memcpy(&f.px[(size_t)y * iw], m.bits + (size_t)y * m.pitch, (size_t)iw * 4);
    cpu->Unmap();
    im.frames.clear(); im.frames.push_back(std::move(f));
    return true;
}

// ---------------------------------------------------------------- 倍率
static float FitZoom() {
    if (!V.img) return 1;
    float z = std::min((float)V.W / V.img->w, (float)V.H / V.img->h);
    if (V.pixel) {
        float best = kLevels[0];
        for (float l : kLevels) if (l <= z + 1e-6f) best = l;
        return best;
    }
    return std::min(1.f, z);
}
static void Snap() { if (V.pixel) { V.ox = std::round(V.ox); V.oy = std::round(V.oy); } }

// 描き直しの印を付けるだけ。描画はメッセージループが垂直同期に合わせて1回行う。
// InvalidateRect は、メインのループが回っていない間（メニュー表示中・サイズ変更中・ダイアログ中）の保険。
// ループが回っているときに呼ぶと WM_PAINT の Render が垂直同期まで止まるので呼ばない
static long long g_loopTick = 0;   // メインのループが最後に回った QPC 時刻
static bool LoopStalled() {
    LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
    return t.QuadPart - g_loopTick >= f.QuadPart * 50 / 1000;
}
void LoopAlive() { LARGE_INTEGER t; QueryPerformanceCounter(&t); g_loopTick = t.QuadPart; }
static void Invalidate() { V.dirty = true; if (LoopStalled()) InvalidateRect(V.hwnd, nullptr, FALSE); OnViewChanged(); }
static void MarkDirty() { V.dirty = true; if (LoopStalled()) InvalidateRect(V.hwnd, nullptr, FALSE); }

static void UpdateAntsTimer() {
    if (V.hasSel) SetTimer(V.hwnd, TM_ANTS, 120, nullptr); else KillTimer(V.hwnd, TM_ANTS);
}

void Reset() {
    if (!V.img) return;
    V.zoom = FitZoom();
    V.ox = (V.W - V.img->w * V.zoom) / 2;
    V.oy = (V.H - V.img->h * V.zoom) / 2;
    Snap();
    V.hasSel = false; UpdateAntsTimer();
    Invalidate();
}

static void ZoomAt(float mx, float my, int dir) {
    if (!V.img) return;
    float z = V.zoom;
    if (V.pixel) {
        float next = 0; bool ok = false;
        if (dir > 0) { for (float l : kLevels) if (l > z + 1e-6f) { next = l; ok = true; break; } }
        else { for (int i = kNumLevels - 1; i >= 0; i--) if (kLevels[i] < z - 1e-6f) { next = kLevels[i]; ok = true; break; } }
        if (!ok) return;
        z = next;
    } else {
        z = std::max(0.02f, std::min(64.f, z * std::pow(1.15f, (float)dir)));
    }
    float ix = (mx - V.ox) / V.zoom, iy = (my - V.oy) / V.zoom;
    V.zoom = z; V.ox = mx - ix * z; V.oy = my - iy * z;
    Snap();
    Invalidate();
}

void ZoomStep(int dir) { ZoomAt(V.W / 2.f, V.H / 2.f, dir); }
float Zoom() { return V.zoom; }
bool Pixel() { return V.pixel; }

void SetPixel(bool on) {
    V.pixel = on;
    if (on && V.img) {
        float best = kLevels[0];
        for (float l : kLevels) if (std::fabs(std::log(l / V.zoom)) < std::fabs(std::log(best / V.zoom))) best = l;
        float cx = V.W / 2.f, cy = V.H / 2.f, ix = (cx - V.ox) / V.zoom, iy = (cy - V.oy) / V.zoom;
        V.zoom = best; V.ox = cx - ix * best; V.oy = cy - iy * best; Snap();
    }
    Invalidate();
}

int Bg() { return V.bg; }
void SetBg(int b) { V.bg = ((b % 4) + 4) % 4; Invalidate(); }
COLORREF Custom() { return V.custom; }
void SetCustom(COLORREF c) { V.custom = c; Invalidate(); }

const Image* Cur() { return V.img.get(); }
bool Failed() { return V.failed; }
const std::vector<uint32_t>& CurPixels() { static std::vector<uint32_t> e; return V.img ? V.img->frames[V.frame].px : e; }

// ---------------------------------------------------------------- 選択
void SelectAll() { if (!V.img) return; V.hasSel = true; V.sx0 = V.sy0 = 0; V.sx1 = V.img->w; V.sy1 = V.img->h; UpdateAntsTimer(); Invalidate(); }
void Deselect() { V.hasSel = false; UpdateAntsTimer(); Invalidate(); }
RECT CropRect() {
    if (V.hasSel) return RECT{ V.sx0, V.sy0, V.sx1, V.sy1 };
    return RECT{ 0, 0, V.img ? V.img->w : 0, V.img ? V.img->h : 0 };
}
bool SelectMode() { return V.selectMode; }
void SetSelectMode(bool on) {
    if (V.selectMode == on) return;
    V.selectMode = on;
    if (!on) {
        if (V.selDrag) { V.selDrag = false; ReleaseCapture(); }
        V.hasSel = false; UpdateAntsTimer();
    }
    Invalidate();
}
bool SelDragging(int& w, int& h) { if (!V.selDrag) return false; w = V.sx1 - V.sx0; h = V.sy1 - V.sy0; return true; }

static POINT ImgPos(int mx, int my) {
    long x = std::lround((mx - V.ox) / V.zoom), y = std::lround((my - V.oy) / V.zoom);
    return POINT{ std::max(0L, std::min((long)V.img->w, x)), std::max(0L, std::min((long)V.img->h, y)) };
}

bool Probe(int& x, int& y, uint32_t& px) {
    if (!V.img || !V.mouseIn) return false;
    int ix = (int)std::floor((V.mx - V.ox) / V.zoom), iy = (int)std::floor((V.my - V.oy) / V.zoom);
    if (ix < 0 || iy < 0 || ix >= V.img->w || iy >= V.img->h) return false;
    x = ix; y = iy; px = V.img->frames[V.frame].px[(size_t)iy * V.img->w + ix];
    return true;
}

// ---------------------------------------------------------------- 画像の入れ替え
static long long QpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static long long QpcFreq() { static long long f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x.QuadPart; }(); return f; }
static long long MsToTicks(int ms) { return (long long)ms * QpcFreq() / 1000; }

// アニメの再生中だけ待ちの精度を上げる（timeBeginPeriod / timeEndPeriod は対で呼ぶ）
static void SetTimerRes(bool on) {
    if (on == V.timerRes) return;
    V.timerRes = on;
    if (on) timeBeginPeriod(1); else timeEndPeriod(1);
}

static void StartAnim() {
    V.playing = V.img && V.img->frames.size() > 1;
    SetTimerRes(V.playing);
    if (!V.playing) return;
    V.animStart = QpcNow();
}
static void StopAnim() { V.playing = false; SetTimerRes(false); }

bool Animated() { return V.img && !V.failed && V.img->frames.size() > 1; }
bool Playing() { return V.playing; }
int FrameIndex() { return V.frame; }
int FrameCount() { return V.img ? (int)V.img->frames.size() : 0; }
static void ShowFrame() {
    if (V.bmp) V.bmp->CopyFromMemory(nullptr, V.img->frames[V.frame].px.data(), V.img->w * 4);
    Invalidate();
}
// 再開は今のコマから。表示開始を「今」として絶対時刻の再生を続ける
void TogglePlay() {
    if (!Animated()) return;
    if (V.playing) StopAnim(); else StartAnim();
    OnViewChanged();
}
void StepFrame(int d) {
    if (!Animated()) return;
    StopAnim();
    int n = (int)V.img->frames.size();
    V.frame = ((V.frame + d) % n + n) % n;
    ShowFrame();
}

// 次のコマまでの残り（ms）。再生していなければ INFINITE
DWORD AnimTimeout() {
    if (!V.playing || !V.img) return INFINITE;
    long long due = V.animStart + MsToTicks(std::max(1, V.img->frames[V.frame].delay));
    long long left = due - QpcNow();
    if (left <= 0) return 0;
    return (DWORD)((left * 1000 + QpcFreq() - 1) / QpcFreq());
}

// 時刻が来ていればコマを進める。遅れていたら今の時刻に合うコマまで飛ばす
void AnimTick() {
    if (!V.playing || !V.img) return;
    const std::vector<Frame>& fr = V.img->frames;
    long long now = QpcNow();
    long long due = V.animStart + MsToTicks(std::max(1, fr[V.frame].delay));
    if (now < due) return;
    long long loop = 0; for (const Frame& f : fr) loop += MsToTicks(std::max(1, f.delay));
    if (now - V.animStart >= loop) {   // 遅れがループ1周分以上（窓のドラッグ中など）：今から数え直す
        V.frame = (V.frame + 1) % (int)fr.size(); V.animStart = now;
    } else {
        while (now >= due) {
            V.animStart = due;
            V.frame = (V.frame + 1) % (int)fr.size();
            due = V.animStart + MsToTicks(std::max(1, fr[V.frame].delay));
        }
    }
    if (V.bmp) V.bmp->CopyFromMemory(nullptr, fr[V.frame].px.data(), V.img->w * 4);
    V.dirty = true;   // 描画はループが垂直同期に合わせて行う
    OnViewChanged();   // ステータスのコマ番号
}

bool SetImage(std::unique_ptr<Image> img, bool keep) {
    if (!V.dc) return false;
    bool had = V.img && !V.failed;
    ComPtr<ID2D1SvgDocument> doc;
    if (img->svg) { if (!PrepareSvg(*img, doc)) return false; }
    else if ((UINT)img->w > V.dc->GetMaximumBitmapSize() || (UINT)img->h > V.dc->GetMaximumBitmapSize()) return false;
    StopAnim(); V.serial++;
    V.img = std::move(img); V.failed = false; V.frame = 0; V.svg = doc;
    if (!MakeImageBitmap()) { V.img.reset(); return false; }
    if (!keep || !had) Reset();
    else {
        if (V.hasSel) {
            V.sx1 = std::min(V.sx1, V.img->w); V.sy1 = std::min(V.sy1, V.img->h);
            if (V.sx0 >= V.sx1 || V.sy0 >= V.sy1) { V.hasSel = false; UpdateAntsTimer(); }
        }
        Invalidate();
    }
    StartAnim();
    return true;
}

void SetFailed() { V.serial++; StopAnim(); V.img.reset(); V.bmp.Reset(); V.svg.Reset(); V.failed = true; V.hasSel = false; UpdateAntsTimer(); Invalidate(); }
void Clear() { V.serial++; StopAnim(); V.img.reset(); V.bmp.Reset(); V.svg.Reset(); V.failed = false; V.hasSel = false; UpdateAntsTimer(); Invalidate(); }
void OnScaleChanged() { V.checker.Reset(); V.textBmp.Reset(); if (V.hwnd) Invalidate(); }

// ---------------------------------------------------------------- 描画
static ID2D1BitmapBrush1* CheckerBrush() {
    if (V.checker) return V.checker.Get();
    int s = 8 * g_scale, n = s * 2;
    std::vector<uint32_t> px((size_t)n * n);
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) px[(size_t)y * n + x] = (((x / s) + (y / s)) & 1) ? 0xFFCCCCCC : 0xFFFFFFFF;
    ComPtr<ID2D1Bitmap1> b;
    if (FAILED(V.dc->CreateBitmap(D2D1::SizeU(n, n), px.data(), n * 4, PbgraProps(), &b))) return nullptr;
    V.dc->CreateBitmapBrush(b.Get(), D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR), &V.checker);
    return V.checker.Get();
}

static ID2D1Bitmap* MessageBitmap() {
    if (V.textBmp) return V.textBmp.Get();
    const wchar_t* msg = lang::T(lang::CANNOT_OPEN);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz; GetTextExtentPoint32W(dc, msg, (int)wcslen(msg), &sz);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = sz.cx; bi.bmiHeader.biHeight = -sz.cy;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    uint32_t* bits = nullptr;
    HBITMAP hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, hb);
    SetBkMode(dc, OPAQUE); SetBkColor(dc, RGB(0, 0, 0)); SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 0, 0, msg, (int)wcslen(msg));
    GdiFlush();
    std::vector<uint32_t> px((size_t)sz.cx * sz.cy);
    for (size_t i = 0; i < px.size(); i++) px[i] = (bits[i] & 0xFF) >= 128 ? 0xFF000000u : 0u;
    SelectObject(dc, ob); DeleteObject(hb); SelectObject(dc, old); DeleteDC(dc);
    ComPtr<ID2D1Bitmap1> b;
    if (FAILED(V.dc->CreateBitmap(D2D1::SizeU(sz.cx, sz.cy), px.data(), sz.cx * 4, PbgraProps(), &b))) return nullptr;
    V.textBmp = b;
    return b.Get();
}

static void RebuildResources() {
    V.serial++;
    MakeImageBitmap();
    V.svg.Reset();
    if (V.img && V.img->svg) V.svg = MakeSvgDoc(*V.img, (float)V.img->w, (float)V.img->h);
}

// 通常モードで縮小表示のとき、表示倍率に縮小したビットマップを作って持つ（パンの間は等倍で転送するだけ）
static ID2D1Bitmap1* EnsureCache(int& cw, int& ch) {
    const Image* im = V.img.get();
    bool want = im && !V.pixel && !im->svg && V.bmp && V.zoom < 1 && im->frames.size() == 1;
    if (want) {
        cw = std::max(1, (int)std::lround(im->w * V.zoom)); ch = std::max(1, (int)std::lround(im->h * V.zoom));
        UINT mx = V.dc->GetMaximumBitmapSize();
        if ((UINT)cw > mx || (UINT)ch > mx || (unsigned long long)cw * ch > 64ull * 1024 * 1024) want = false;
    }
    if (!want) { V.cache.Reset(); return nullptr; }
    if (V.cache && V.cacheW == cw && V.cacheH == ch && V.cacheSerial == V.serial) return V.cache.Get();
    V.cache.Reset();
    ComPtr<ID2D1Bitmap1> c;
    if (FAILED(V.dc->CreateBitmap(D2D1::SizeU(cw, ch), nullptr, 0, PbgraProps(D2D1_BITMAP_OPTIONS_TARGET), &c))) return nullptr;
    V.dc->SetTarget(c.Get());
    V.dc->BeginDraw();
    V.dc->SetTransform(D2D1::Matrix3x2F::Identity());
    V.dc->Clear(D2D1::ColorF(0, 0));
    D2D1_RECT_F dst = D2D1::RectF(0, 0, (float)cw, (float)ch);
    V.dc->DrawBitmap(V.bmp.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    HRESULT hr = V.dc->EndDraw();
    V.dc->SetTarget(V.target.Get());
    if (FAILED(hr)) return nullptr;
    V.cache = c; V.cacheW = cw; V.cacheH = ch; V.cacheSerial = V.serial;
    return V.cache.Get();
}

static void Render() {
    if (!V.dc || !V.target) return;
    V.dirty = false;
    ID2D1DeviceContext* dc = V.dc.Get();
    int cw = 0, ch = 0;
    ID2D1Bitmap1* cache = EnsureCache(cw, ch);
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    dc->Clear(D2D1::ColorF(0x808080));
    if (V.img) {
        const Image& im = *V.img;
        float x = V.ox, y = V.oy, w = im.w * V.zoom, h = im.h * V.zoom;
        if (cache) { x = std::round(x); y = std::round(y); w = (float)cw; h = (float)ch; }   // キャッシュは整数位置に等倍で置く
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
        bool clipped = false;
        if (V.pixel) { dc->PushAxisAlignedClip(D2D1::RectF(std::round(r.left), std::round(r.top), std::round(r.right), std::round(r.bottom)), D2D1_ANTIALIAS_MODE_ALIASED); clipped = true; }
        if (V.bg == 0) { if (ID2D1BitmapBrush1* b = CheckerBrush()) dc->FillRectangle(r, b); }
        else {
            COLORREF c = V.bg == 1 ? RGB(0, 0, 0) : V.bg == 2 ? RGB(255, 255, 255) : V.custom;
            if (!V.bgBrush) dc->CreateSolidColorBrush(D2D1::ColorF(0), &V.bgBrush);
            if (V.bgBrush) {
                V.bgBrush->SetColor(D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f));
                dc->FillRectangle(r, V.bgBrush.Get());
            }
        }
        if (im.svg) {
            if (V.svg && V.dc5) {
                dc->SetTransform(D2D1::Matrix3x2F::Scale(V.zoom, V.zoom) * D2D1::Matrix3x2F::Translation(x, y));
                dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                V.dc5->DrawSvgDocument(V.svg.Get());
                dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                dc->SetTransform(D2D1::Matrix3x2F::Identity());
            }
        } else if (cache) {
            dc->DrawBitmap(cache, &r, 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        } else if (V.bmp) {
            dc->DrawBitmap(V.bmp.Get(), &r, 1.f, V.pixel || V.zoom >= 1 ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);   // 拡大は補間なし、縮小だけ高品質
        }
        if (clipped) dc->PopAxisAlignedClip();

        if (V.hasSel) {
            float rx = std::round(V.ox + V.sx0 * V.zoom) + 0.5f, ry = std::round(V.oy + V.sy0 * V.zoom) + 0.5f;
            float rw = std::max(0.f, std::round((V.sx1 - V.sx0) * V.zoom) - 1), rh = std::max(0.f, std::round((V.sy1 - V.sy0) * V.zoom) - 1);
            D2D1_RECT_F sr = D2D1::RectF(rx, ry, rx + rw, ry + rh);
            if (!V.whiteBrush) dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &V.whiteBrush);
            if (!V.blackBrush) dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &V.blackBrush);
            ComPtr<ID2D1StrokeStyle>& ss = V.antsStyle[V.ants & 7];   // 点線の位相ごとに1つ作って使い回す
            if (!ss) {
                float dashes[2] = { 4, 4 };
                V.fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                    D2D1_LINE_JOIN_MITER, 10, D2D1_DASH_STYLE_CUSTOM, (float)(V.ants & 7)), dashes, 2, &ss);
            }
            if (V.whiteBrush) dc->DrawRectangle(sr, V.whiteBrush.Get(), 1.f);
            if (V.blackBrush && ss) dc->DrawRectangle(sr, V.blackBrush.Get(), 1.f, ss.Get());
        }
    } else if (V.failed) {
        if (ID2D1Bitmap* tb = MessageBitmap()) {
            D2D1_SIZE_F s = tb->GetSize(); s.width *= g_scale; s.height *= g_scale;   // 整数倍
            float x = std::floor((V.W - s.width) / 2), y = std::floor((V.H - s.height) / 2);
            dc->DrawBitmap(tb, D2D1::RectF(x, y, x + s.width, y + s.height), 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
    }
    HRESULT hr = dc->EndDraw();
    if (SUCCEEDED(hr)) hr = V.sc->Present(1, 0);   // 垂直同期1回に1フレーム
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        ReleaseDevices();
        if (InitDevices()) { RebuildResources(); MarkDirty(); }
    }
    ValidateRect(V.hwnd, nullptr);
}

bool NeedsFrame() { return V.dirty && V.dc; }
HANDLE FrameHandle() { return V.frameWait; }
void RenderIfDirty() { if (V.dirty) Render(); }

// ---------------------------------------------------------------- ウィンドウ
static void Track() { TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, V.hwnd, 0 }; TrackMouseEvent(&t); }

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Render(); EndPaint(h, &ps); return 0; }
    case WM_SIZE: {
        int nw = std::max(1, (int)LOWORD(l)), nh = std::max(1, (int)HIWORD(l));
        if (V.dc && (nw != V.W || nh != V.H)) {
            // 中心を保つ
            V.ox += (nw - V.W) / 2.f; V.oy += (nh - V.H) / 2.f; Snap();
            V.W = nw; V.H = nh;
            if (!CreateSwapAndTarget()) { ReleaseDevices(); if (InitDevices()) RebuildResources(); }
            MarkDirty(); Render();   // サイズ変更中は待たずに描く
        }
        return 0;
    }
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return TRUE; } break;
    case WM_LBUTTONDOWN:
        SetFocus(V.parent);
        if (V.img) {
            if (V.selectMode) {   // 範囲選択がオン：左ドラッグで選択（パンしない）
                POINT p = ImgPos(GET_X_LPARAM(l), GET_Y_LPARAM(l));
                V.selDrag = true; V.ax = p.x; V.ay = p.y; V.hasSel = false; UpdateAntsTimer(); SetCapture(h); Invalidate();
            } else { V.pressing = true; V.panning = false; V.pmx = GET_X_LPARAM(l); V.pmy = GET_Y_LPARAM(l); V.pox = V.ox; V.poy = V.oy; SetCapture(h); }
        }
        return 0;
    case WM_RBUTTONDOWN: SetFocus(V.parent); return 0;   // 右ボタンは何もしない
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (!V.mouseIn) { V.mouseIn = true; Track(); }
        V.mx = x; V.my = y;
        if (V.pressing && !V.panning && (std::abs(x - V.pmx) > GetSystemMetrics(SM_CXDRAG) || std::abs(y - V.pmy) > GetSystemMetrics(SM_CYDRAG))) V.panning = true;
        if (V.panning) { V.ox = V.pox + (x - V.pmx); V.oy = V.poy + (y - V.pmy); Snap(); }   // 位置は押した点からの差
        if (V.selDrag && V.img) {
            POINT q = ImgPos(x, y);
            int x0 = std::min<int>(V.ax, q.x), y0 = std::min<int>(V.ay, q.y), x1 = std::max<int>(V.ax, q.x), y1 = std::max<int>(V.ay, q.y);
            V.sx0 = x0; V.sy0 = y0; V.sx1 = x1; V.sy1 = y1;
            V.hasSel = x1 > x0 && y1 > y0; UpdateAntsTimer();
        }
        // 描き直しが要るのはパンと範囲選択のドラッグ中だけ。ふだんはステータスの更新のみ
        if (V.panning || V.selDrag) Invalidate(); else OnViewChanged();
        return 0;
    }
    case WM_MOUSELEAVE: V.mouseIn = false; OnViewChanged(); return 0;
    case WM_LBUTTONUP:
        if (V.pressing) {
            bool click = !V.panning;
            V.pressing = V.panning = false; ReleaseCapture();
            if (click) TogglePlay();   // 動かさずに離した：再生・停止（静止画では何もしない）
        } else if (V.selDrag) { V.selDrag = false; ReleaseCapture(); Invalidate(); }
        return 0;
    case WM_CAPTURECHANGED: V.pressing = V.panning = false; if (V.selDrag) { V.selDrag = false; Invalidate(); } return 0;
    case WM_CONTEXTMENU: return 0;
    case WM_MOUSEWHEEL: {
        // ホイール / Ctrl+ホイール：カーソル位置を中心に拡大縮小。Shift+ホイール：前後の画像へ
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p);
        V.wheelAcc += GET_WHEEL_DELTA_WPARAM(w);
        while (std::abs(V.wheelAcc) >= WHEEL_DELTA) {
            int dir = V.wheelAcc > 0 ? 1 : -1;
            V.wheelAcc -= dir * WHEEL_DELTA;
            if (LOWORD(w) & MK_SHIFT) NavigateRel(-dir);
            else ZoomAt((float)p.x, (float)p.y, dir);
        }
        return 0;
    }
    case WM_MOUSEHWHEEL: {   // 横スクロールとして届いたホイールは画像送り（右へ＝次）
        V.hwheelAcc += GET_WHEEL_DELTA_WPARAM(w);
        while (std::abs(V.hwheelAcc) >= WHEEL_DELTA) {
            int dir = V.hwheelAcc > 0 ? 1 : -1;
            V.hwheelAcc -= dir * WHEEL_DELTA;
            NavigateRel(dir);
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TM_ANTS) { V.ants = (V.ants + 1) % 8; MarkDirty(); }
        return 0;
    case WM_DESTROY: StopAnim(); return 0;
    case WM_DROPFILES: OnFilesDropped((HDROP)w); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool Create(HWND parent) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLIVView";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    V.parent = parent;
    V.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"TLIVView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, 100, parent, nullptr, wc.hInstance, nullptr);
    if (!V.hwnd) return false;
    RECT rc; GetClientRect(V.hwnd, &rc);
    V.W = std::max(1L, rc.right); V.H = std::max(1L, rc.bottom);
    return InitDevices();
}

HWND Hwnd() { return V.hwnd; }
void Move(int x, int y, int w, int h) { SetWindowPos(V.hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); }

}  // namespace viewer

