#include "decode.h"
#include "util.h"
#include "bytes.h"
#include <shlwapi.h>
#include <algorithm>
#include <cstring>

static uint32_t Crc32(const uint8_t* p, size_t n) {
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; t[i] = v; }
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
static void PutBe32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }

// フレームを PBGRA で取り出す
static bool FrameToPixels(IWICBitmapSource* src, int& w, int& h, std::vector<uint32_t>& px) {
    UINT uw, uh;
    if (FAILED(src->GetSize(&uw, &uh)) || !uw || !uh || uw > 32768 || uh > 32768) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g_wic->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    if ((ULONGLONG)uw * uh * 4 > kMemLimit) return false;   // 1フレームだけで上限を超える
    w = uw; h = uh;
    try { px.assign((size_t)w * h, 0); } catch (...) { return false; }
    return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size() * 4, (BYTE*)px.data()));
}

bool DecodePngResource(const void* data, size_t len, int& w, int& h, std::vector<uint32_t>& px) {
    ComPtr<IStream> s = MemStream(data, len);
    if (!s) return false;
    ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr;
    return SUCCEEDED(g_wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))
        && SUCCEEDED(dec->GetFrame(0, &fr)) && FrameToPixels(fr.Get(), w, h, px);
}

// src OVER dst（プリマルチ）
static inline uint32_t Over(uint32_t s, uint32_t d) {
    uint32_t sa = s >> 24;
    if (sa == 255) return s;
    if (sa == 0) return d;
    uint32_t ia = 255 - sa, r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t v = ((s >> sh) & 255) + (((d >> sh) & 255) * ia + 127) / 255;
        r |= (v > 255 ? 255 : v) << sh;
    }
    return r;
}

struct Sub { int x = 0, y = 0, w = 0, h = 0; std::vector<uint32_t> px; };

static void Draw(std::vector<uint32_t>& cv, int cw, int ch, const Sub& s, bool over) {
    for (int y = 0; y < s.h; y++) {
        int cy = s.y + y; if (cy < 0 || cy >= ch) continue;
        for (int x = 0; x < s.w; x++) {
            int cx = s.x + x; if (cx < 0 || cx >= cw) continue;
            uint32_t sp = s.px[(size_t)y * s.w + x];
            uint32_t& d = cv[(size_t)cy * cw + cx];
            d = over ? Over(sp, d) : sp;
        }
    }
}
static void ClearRect(std::vector<uint32_t>& cv, int cw, int ch, int x0, int y0, int w, int h) {
    for (int y = std::max(0, y0); y < std::min(ch, y0 + h); y++)
        for (int x = std::max(0, x0); x < std::min(cw, x0 + w); x++) cv[(size_t)y * cw + x] = 0;
}

// ---------------- APNG（チャンクを分解して各フレームを単体 PNG に組み直す）
static bool DecodeApng(const std::vector<uint8_t>& d, Image& out) {
    if (d.size() < 33 || !HasPngSig(d.data(), d.size())) return false;
    struct Fc { uint32_t w, h, x, y, dn, dd; int disp, blend; std::vector<uint8_t> idat; };
    std::vector<Fc> fcs;
    std::vector<uint8_t> pre; // 最初の fcTL より前のその他チャンク（PLTE, tRNS など、丸ごと）
    uint8_t ihdr[13] = {}; bool haveActl = false, haveIhdr = false;
    int totalFc = 0; bool capped = false;   // fcTL の総数と、kMaxFrames を超えて集めなかったか
    ForEachPngChunk(d.data(), d.size(), [&](const uint8_t* t, const uint8_t* body, uint32_t len) {
        if (!memcmp(t, "IHDR", 4) && len == 13) { memcpy(ihdr, body, 13); haveIhdr = true; }
        else if (!memcmp(t, "acTL", 4)) haveActl = true;
        else if (!memcmp(t, "fcTL", 4) && len >= 26) {
            totalFc++;
            if ((int)fcs.size() >= kMaxFrames) { capped = true; return true; }   // 上限を超えた分は集めない（続く IDAT / fdAT も捨てる）
            Fc f; f.w = Be32(body + 4); f.h = Be32(body + 8); f.x = Be32(body + 12); f.y = Be32(body + 16);
            f.dn = (body[20] << 8) | body[21]; f.dd = (body[22] << 8) | body[23]; f.disp = body[24]; f.blend = body[25];
            fcs.push_back(std::move(f));
        }
        else if (!memcmp(t, "IDAT", 4)) { if (!fcs.empty() && !capped) fcs.back().idat.insert(fcs.back().idat.end(), body, body + len); }
        else if (!memcmp(t, "fdAT", 4)) { if (len >= 4 && !fcs.empty() && !capped) fcs.back().idat.insert(fcs.back().idat.end(), body + 4, body + len); }
        else if (!memcmp(t, "IEND", 4)) {}
        else if (fcs.empty()) pre.insert(pre.end(), body - 8, body + len + 4);   // 長さ・型・本体・CRC をそのまま
        return true;
    });
    if (!haveActl || !haveIhdr || fcs.size() < 2) return false;
    uint32_t cw = Be32(ihdr), ch = Be32(ihdr + 4);
    if (!cw || !ch || cw > 32768 || ch > 32768) return false;
    if ((ULONGLONG)cw * ch * 4 > kMemLimit) return false;
    std::vector<uint32_t> cv((size_t)cw * ch, 0), saved;
    ULONGLONG used = 0, fbytes = (ULONGLONG)cw * ch * 4;
    out.framesTotal = totalFc;
    for (size_t i = 0; i < fcs.size(); i++) {
        if (used + fbytes > kMemLimit) { out.memLimited = true; break; }   // ここまでで止める
        Fc& f = fcs[i];
        if (f.idat.empty() || !f.w || !f.h) return false;
        std::vector<uint8_t> png(kPngSig, kPngSig + 8);
        uint8_t hh[13]; memcpy(hh, ihdr, 13);
        hh[0] = f.w >> 24; hh[1] = f.w >> 16; hh[2] = f.w >> 8; hh[3] = f.w;
        hh[4] = f.h >> 24; hh[5] = f.h >> 16; hh[6] = f.h >> 8; hh[7] = f.h;
        auto chunk = [&](const char* ty, const uint8_t* b, size_t n) {
            PutBe32(png, (uint32_t)n); size_t s0 = png.size(); png.insert(png.end(), ty, ty + 4);
            if (n) png.insert(png.end(), b, b + n);
            PutBe32(png, Crc32(&png[s0], png.size() - s0));
        };
        chunk("IHDR", hh, 13);
        png.insert(png.end(), pre.begin(), pre.end());
        chunk("IDAT", f.idat.data(), f.idat.size());
        chunk("IEND", nullptr, 0);
        ComPtr<IStream> s = MemStream(png.data(), png.size());
        if (!s) return false;
        ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr; Sub sub;
        bool ok = SUCCEEDED(g_wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))
            && SUCCEEDED(dec->GetFrame(0, &fr)) && FrameToPixels(fr.Get(), sub.w, sub.h, sub.px);
        if (!ok || sub.w != (int)f.w || sub.h != (int)f.h) return false;
        sub.x = f.x; sub.y = f.y;
        int disp = f.disp; if (i == 0 && disp == 2) disp = 1;
        if (disp == 2) saved = cv;
        Draw(cv, cw, ch, sub, f.blend == 1);
        Frame fo; fo.px = cv;
        uint32_t dd = f.dd ? f.dd : 100; int ms = (int)((uint64_t)f.dn * 1000 / dd);
        fo.delay = ms <= 10 ? 100 : ms;
        out.frames.push_back(std::move(fo)); used += fbytes;
        if (disp == 1) ClearRect(cv, cw, ch, f.x, f.y, f.w, f.h);
        else if (disp == 2) cv = saved;
    }
    out.w = cw; out.h = ch;
    out.frameLimited = capped && !out.memLimited;
    return true;
}

// ---------------- GIF
static bool QueryU(IWICMetadataQueryReader* q, const wchar_t* name, UINT& v) {
    if (!q) return false;
    PROPVARIANT pv; PropVariantInit(&pv);
    bool ok = SUCCEEDED(q->GetMetadataByName(name, &pv));
    if (ok) {
        switch (pv.vt) { case VT_UI1: v = pv.bVal; break; case VT_UI2: v = pv.uiVal; break; case VT_UI4: v = pv.ulVal; break; default: ok = false; }
    }
    PropVariantClear(&pv);
    return ok;
}

static bool DecodeGif(IWICBitmapDecoder* dec, UINT n, Image& out) {
    ComPtr<IWICMetadataQueryReader> dq; UINT cw = 0, ch = 0;
    dec->GetMetadataQueryReader(&dq);
    QueryU(dq.Get(), L"/logscrdesc/Width", cw); QueryU(dq.Get(), L"/logscrdesc/Height", ch);
    std::vector<uint32_t> cv, saved;
    ULONGLONG used = 0;
    out.framesTotal = (int)n;
    UINT cnt = std::min<UINT>(n, (UINT)kMaxFrames);
    for (UINT i = 0; i < cnt; i++) {
        if (cw && ch && used + (ULONGLONG)cw * ch * 4 > kMemLimit) { out.memLimited = true; break; }
        ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(i, &fr))) return false;
        Sub s; if (!FrameToPixels(fr.Get(), s.w, s.h, s.px)) return false;
        if (!cw || !ch) { cw = s.w; ch = s.h; }
        if (cv.empty()) {
            if ((ULONGLONG)cw * ch * 4 > kMemLimit) return false;
            cv.assign((size_t)cw * ch, 0);
        }
        ComPtr<IWICMetadataQueryReader> q; fr->GetMetadataQueryReader(&q);
        UINT l = 0, t = 0, delay = 0, disp = 0;
        QueryU(q.Get(), L"/imgdesc/Left", l); QueryU(q.Get(), L"/imgdesc/Top", t);
        QueryU(q.Get(), L"/grctlext/Delay", delay); QueryU(q.Get(), L"/grctlext/Disposal", disp);
        s.x = l; s.y = t;
        if (disp == 3) saved = cv;
        Draw(cv, cw, ch, s, true);
        Frame fo; fo.px = cv; fo.delay = delay < 2 ? 100 : delay * 10;
        out.frames.push_back(std::move(fo)); used += (ULONGLONG)cw * ch * 4;
        if (disp == 2) ClearRect(cv, cw, ch, s.x, s.y, s.w, s.h);
        else if (disp == 3) cv = saved;
    }
    out.w = cw; out.h = ch;
    if (!out.memLimited && n > cnt) out.frameLimited = true;
    return true;
}

// ---------------- EXIF の向き
static int TiffOrient(const uint8_t* t, size_t n) {
    if (n < 8) return 0;
    bool le;
    if (t[0] == 'I' && t[1] == 'I') le = true; else if (t[0] == 'M' && t[1] == 'M') le = false; else return 0;
    auto r16 = [&](const uint8_t* p) -> uint32_t { return le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]); };
    auto r32 = [&](const uint8_t* p) -> uint32_t { return le ? Le32(p) : Be32(p); };
    if (r16(t + 2) != 42) return 0;
    uint64_t off = r32(t + 4);
    if (off + 2 > n) return 0;
    uint32_t cnt = r16(t + off);
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t e = off + 2 + (uint64_t)i * 12;
        if (e + 12 > n) break;
        if (r16(t + e) == 274 && r16(t + e + 2) == 3 && r32(t + e + 4) >= 1) { uint32_t v = r16(t + e + 8); return v >= 1 && v <= 8 ? (int)v : 0; }
    }
    return 0;
}
// "Exif\0\0" が付いていれば飛ばして TIFF として読む
static int ExifBlobOrient(const uint8_t* p, size_t n) {
    SkipExifHeader(p, n);
    return TiffOrient(p, n);
}
static int FileOrient(const std::vector<uint8_t>& d) {
    const uint8_t* p = d.data(); size_t n = d.size();
    int o = 0;
    if (ForEachJpegSegment(p, n, [&](uint8_t m, const uint8_t* b, size_t bn) {   // JPEG の APP1
            if (m == 0xE1 && bn >= 6 && !memcmp(b, "Exif\0\0", 6)) { o = TiffOrient(b + 6, bn - 6); return false; }
            return true; })) return o;
    if (ForEachPngChunk(p, n, [&](const uint8_t* t, const uint8_t* b, uint32_t len) {   // PNG eXIf
            if (!memcmp(t, "eXIf", 4)) { o = ExifBlobOrient(b, len); return false; }
            return true; })) return o;
    if (ForEachWebpChunk(p, n, [&](const uint8_t* t, const uint8_t* b, size_t len) {   // WebP EXIF
            if (!memcmp(t, "EXIF", 4)) { o = ExifBlobOrient(b, len); return false; }
            return true; })) return o;
    return TiffOrient(p, n);   // TIFF
}

// 向き 2〜8 をかける。フレームごとに1枚ずつ作り直すので、増えるのは1フレーム分だけ
static void ApplyOrient(Image& im, int o) {
    int w = im.w, h = im.h;
    bool sw = o >= 5;
    int nw = sw ? h : w, nh = sw ? w : h;
    for (auto& f : im.frames) {
        std::vector<uint32_t> d((size_t)nw * nh);
        for (int y = 0; y < h; y++) {
            const uint32_t* sp = &f.px[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                int dx, dy;
                switch (o) {
                    case 2: dx = w - 1 - x; dy = y; break;
                    case 3: dx = w - 1 - x; dy = h - 1 - y; break;
                    case 4: dx = x; dy = h - 1 - y; break;
                    case 5: dx = y; dy = x; break;
                    case 6: dx = h - 1 - y; dy = x; break;
                    case 7: dx = h - 1 - y; dy = w - 1 - x; break;
                    default: dx = y; dy = w - 1 - x; break;   // 8
                }
                d[(size_t)dy * nw + dx] = sp[x];
            }
        }
        f.px = std::move(d);
    }
    im.w = nw; im.h = nh;
}

static bool DecodeFileRaw(const std::wstring& path, Image& out, std::vector<uint8_t>& buf);

// デコード直後の共通の出口。ここで EXIF の向きをかける
bool DecodeFile(const std::wstring& path, Image& out) {
    std::vector<uint8_t> buf;
    if (!DecodeFileRaw(path, out, buf)) return false;
    if (!out.svg) {
        int o = FileOrient(buf);
        if (o >= 2) { out.orient = o; ApplyOrient(out, o); }
        else if (o == 1) out.orient = 1;
    }
    return true;
}

static bool DecodeFileRaw(const std::wstring& path, Image& out, std::vector<uint8_t>& buf) {
    out = Image();
    if (!ReadFileAll(path, buf)) return false;
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!_wcsicmp(ext, L".svg")) {
        out.svg = true; out.svgData.assign(buf.begin(), buf.end());
        return true;
    }
    if (DecodeApng(buf, out)) return true;
    out.frames.clear(); out.framesTotal = 0; out.memLimited = false; out.frameLimited = false;

    ComPtr<IStream> s = MemStream(buf.data(), buf.size());
    if (!s) return false;
    ComPtr<IWICBitmapDecoder> dec;
    bool ok = SUCCEEDED(g_wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec));
    if (ok) {
        UINT n = 0; GUID fmt{}; dec->GetFrameCount(&n); dec->GetContainerFormat(&fmt);
        if (n == 0) ok = false;
        else if (fmt == GUID_ContainerFormatGif && n > 1) ok = DecodeGif(dec.Get(), n, out);
        else if (fmt == GUID_ContainerFormatIco) {
            // 一番大きいフレームを選ぶ
            UINT best = 0, bw = 0;
            for (UINT i = 0; i < n; i++) { ComPtr<IWICBitmapFrameDecode> f; UINT w, h; if (SUCCEEDED(dec->GetFrame(i, &f)) && SUCCEEDED(f->GetSize(&w, &h)) && w > bw) { bw = w; best = i; } }
            ComPtr<IWICBitmapFrameDecode> f; Frame fo;
            ok = SUCCEEDED(dec->GetFrame(best, &f)) && FrameToPixels(f.Get(), out.w, out.h, fo.px);
            if (ok) out.frames.push_back(std::move(fo));
        } else {
            // 静止画。複数フレーム（アニメ WebP など）は WIC が返すものをそのまま再生
            UINT cnt = std::min<UINT>(n, (UINT)kMaxFrames);
            ULONGLONG used = 0;
            if (n > 1) out.framesTotal = (int)n;
            for (UINT i = 0; i < cnt && ok; i++) {
                if (i > 0 && used + (ULONGLONG)out.w * out.h * 4 > kMemLimit) { out.memLimited = true; break; }
                ComPtr<IWICBitmapFrameDecode> f; Frame fo; int w, h;
                ok = SUCCEEDED(dec->GetFrame(i, &f)) && FrameToPixels(f.Get(), w, h, fo.px);
                if (!ok) break;
                if (i == 0) { out.w = w; out.h = h; }
                else if (w != out.w || h != out.h) { break; }
                used += (ULONGLONG)w * h * 4;
                out.frames.push_back(std::move(fo));
            }
            if (out.frames.empty()) ok = false;
            else if (!out.memLimited && n > cnt && out.frames.size() == cnt) out.frameLimited = true;
        }
    }
    return ok && out.w > 0 && out.h > 0 && !out.frames.empty();
}
