#include "clipboard.h"
#include "util.h"

static HGLOBAL GlobalCopy(const void* d, size_t n) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    if (g) {
        void* p = GlobalLock(g);
        if (!p) { GlobalFree(g); return nullptr; }
        memcpy(p, d, n); GlobalUnlock(g);
    }
    return g;
}

// ストレート BGRA を PNG にして、HGLOBAL に入れて返す（失敗したら null）
static HGLOBAL MakePng(const std::vector<uint32_t>& px, int w, int h) {
    HGLOBAL hpng = nullptr;
    ComPtr<IStream> st; ComPtr<IWICBitmapEncoder> enc; ComPtr<IWICBitmapFrameEncode> fr; ComPtr<IPropertyBag2> pb;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &st)) && SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, &pb)) && SUCCEEDED(fr->Initialize(pb.Get()))) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        fr->SetSize(w, h); fr->SetPixelFormat(&pf);
        if (SUCCEEDED(fr->WritePixels(h, w * 4, (UINT)px.size() * 4, (BYTE*)px.data())) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) {
            STATSTG ss; HGLOBAL hg;
            if (SUCCEEDED(st->Stat(&ss, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(st.Get(), &hg))) {
                void* p = GlobalLock(hg); hpng = GlobalCopy(p, (size_t)ss.cbSize.QuadPart); GlobalUnlock(hg);
            }
        }
    }
    return hpng;
}

// ストレート BGRA を DIBV5（下から上、BGRA）にして、HGLOBAL に入れて返す
static HGLOBAL MakeDib(const std::vector<uint32_t>& px, int w, int h) {
    size_t hs = sizeof(BITMAPV5HEADER), bytes = (size_t)w * h * 4;
    HGLOBAL hdib = GlobalAlloc(GMEM_MOVEABLE, hs + bytes);
    if (hdib) {
        BYTE* p = (BYTE*)GlobalLock(hdib);
        if (!p) { GlobalFree(hdib); return nullptr; }
        BITMAPV5HEADER* bh = (BITMAPV5HEADER*)p; memset(bh, 0, hs);
        bh->bV5Size = (DWORD)hs; bh->bV5Width = w; bh->bV5Height = h; bh->bV5Planes = 1; bh->bV5BitCount = 32;
        bh->bV5Compression = BI_BITFIELDS; bh->bV5SizeImage = (DWORD)bytes;
        bh->bV5RedMask = 0x00FF0000; bh->bV5GreenMask = 0x0000FF00; bh->bV5BlueMask = 0x000000FF; bh->bV5AlphaMask = 0xFF000000;
        bh->bV5CSType = LCS_sRGB; bh->bV5Intent = LCS_GM_IMAGES;
        for (int y = 0; y < h; y++) memcpy(p + hs + (size_t)(h - 1 - y) * w * 4, &px[(size_t)y * w], (size_t)w * 4);
        GlobalUnlock(hdib);
    }
    return hdib;
}

CopyResult CopyToClipboard(HWND owner, const uint32_t* src, int srcW, const RECT& r) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return CopyResult::Busy;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        px[(size_t)y * w + x] = Unpremultiply(src[(size_t)(y + r.top) * srcW + (x + r.left)]);
    HGLOBAL hpng = MakePng(px, w, h), hdib = MakeDib(px, w, h);
    if (!hpng && !hdib) return CopyResult::NoMemory;   // どちらも作れなかった（空のクリップボードで「コピーした」と出さない）
    if (!OpenClipboard(owner)) { if (hpng) GlobalFree(hpng); if (hdib) GlobalFree(hdib); return CopyResult::Busy; }
    EmptyClipboard();
    if (hpng) SetClipboardData(RegisterClipboardFormatW(L"PNG"), hpng);
    if (hdib) SetClipboardData(CF_DIBV5, hdib);
    CloseClipboard();
    return CopyResult::Ok;
}
