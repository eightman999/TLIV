#include "meta.h"
#include "inflate.h"
#include "util.h"
#include "bytes.h"
#include "exiftags.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <new>

namespace {

typedef std::wstring ws;

// ---------------------------------------------------------------- 文字
static ws Commas(unsigned long long v) {
    wchar_t b[32]; swprintf_s(b, L"%llu", v);
    ws s = b, r;
    for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) r += L','; r += s[i]; }
    return r;
}

static bool Utf8Valid(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n;) {
        uint8_t c = p[i];
        int k = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (k < 0 || i + k >= n + (k == 0 ? 1 : 0)) return false;
        for (int j = 1; j <= k; j++) if ((p[i + j] & 0xC0) != 0x80) return false;
        if (k == 2 && c == 0xE0 && p[i + 1] < 0xA0) return false;
        if (k == 2 && c == 0xED && p[i + 1] >= 0xA0) return false;
        if (k == 3 && c == 0xF0 && p[i + 1] < 0x90) return false;
        if (k == 3 && c == 0xF4 && p[i + 1] >= 0x90) return false;
        i += k + 1;
    }
    return true;
}

static ws Utf8ToWide(const uint8_t* p, size_t n) {
    if (!n) return ws();
    int len = MultiByteToWideChar(CP_UTF8, 0, (const char*)p, (int)n, nullptr, 0);
    ws w(len, L'\0');
    if (len) MultiByteToWideChar(CP_UTF8, 0, (const char*)p, (int)n, &w[0], len);
    return w;
}
static ws Latin1ToWide(const uint8_t* p, size_t n) { ws w(n, L'\0'); for (size_t i = 0; i < n; i++) w[i] = p[i]; return w; }
// 正しい UTF-8 として読めれば UTF-8、だめなら Latin-1（tEXt / zTXt 用）
static ws AutoToWide(const uint8_t* p, size_t n) { return Utf8Valid(p, n) ? Utf8ToWide(p, n) : Latin1ToWide(p, n); }

// ---- 上限つきの文字変換。文字に直すのは kTextMax の範囲のバイトだけ。UTF-8 は文字の途中で切らない（続きバイトの間は戻る）。
// 省いた文字数は more に入れる（切らなければ 0）
static ws Utf8ToWideLim(const uint8_t* p, size_t n, size_t& more) {
    more = 0;
    // kTextMax 文字ぶんのバイト数を、先頭バイト（続きバイトでないもの）を数えて求める。切るのは文字の境目だけ
    size_t cut = n, chars = 0;
    for (size_t i = 0; i < n; i++) {
        if ((p[i] & 0xC0) == 0x80) continue;
        if (chars == kTextMax) { cut = i; break; }
        chars++;
    }
    for (size_t i = cut; i < n; i++) if ((p[i] & 0xC0) != 0x80) more++;
    return Utf8ToWide(p, cut);
}
static ws Latin1ToWideLim(const uint8_t* p, size_t n, size_t& more) {
    size_t cut = std::min(n, kTextMax);
    more = n - cut;
    return Latin1ToWide(p, cut);
}
// 全体が正しい UTF-8 かどうかは、上限で切る前の全体で決める（切ったあとだけで決めると、表示が変わってしまう）
static ws AutoToWideLim(const uint8_t* p, size_t n, size_t& more) { return Utf8Valid(p, n) ? Utf8ToWideLim(p, n, more) : Latin1ToWideLim(p, n, more); }

static ws TruncMark(size_t more) { return L"... (truncated, " + Commas(more) + L" more characters)"; }
// 1行ものは " ..." を同じ行に、本文（複数行）は次の行に注記する
static ws AutoInline(const uint8_t* p, size_t n) { size_t more; ws r = AutoToWideLim(p, n, more); if (more) r += L" " + TruncMark(more); return r; }
static ws AutoBody(const uint8_t* p, size_t n) { size_t more; ws r = AutoToWideLim(p, n, more); if (more) r += L"\n" + TruncMark(more); return r; }
static ws Utf8Body(const uint8_t* p, size_t n) { size_t more; ws r = Utf8ToWideLim(p, n, more); if (more) r += L"\n" + TruncMark(more); return r; }
static ws Utf8Inline(const uint8_t* p, size_t n) { size_t more; ws r = Utf8ToWideLim(p, n, more); if (more) r += L" " + TruncMark(more); return r; }
// すでに文字になっている1行を kTextMax 文字までにする（サロゲートペアの途中では切らない）
static ws CapLine(ws l) {
    if (l.size() <= kTextMax) return l;
    size_t cut = kTextMax, more = l.size() - cut;
    if (cut > 0 && l[cut - 1] >= 0xD800 && l[cut - 1] <= 0xDBFF) { cut--; more++; }
    l.resize(cut);
    return l + L" " + TruncMark(more);
}

static void Line(ws& s, const ws& k, const ws& v) { s += k; s += L": "; s += v; s += L'\n'; }
static void LineF(ws& s, const wchar_t* k, const wchar_t* fmt, ...) {
    wchar_t b[256]; va_list a; va_start(a, fmt); vswprintf_s(b, fmt, a); va_end(a);
    Line(s, k, b);
}

// ---------------------------------------------------------------- PNG
struct Txt { ws head, body; bool json = false; };
struct Png {
    bool valid = false;
    uint32_t w = 0, h = 0; int depth = 0, ctype = 0, inter = 0;
    std::vector<std::string> chunks;
    int plte = -1; bool hasTrns = false; std::vector<uint8_t> trns;
    bool srgb = false, hasGama = false; double gama = 0; bool hasIccp = false; ws iccp;
    bool hasPhys = false; uint32_t ppx = 0, ppy = 0; int phyUnit = 0;
    bool actl = false; uint32_t frames = 0, plays = 0; double totalMs = 0;
    std::vector<Txt> texts;
    size_t textChars = 0;       // texts に入れた文字数の合計（kPanelMax を超えたら、あとのテキストは集めない）
};

static bool IsJsonLike(const ws& b) {
    for (wchar_t c : b) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xFEFF || c == 0) continue;
        return c == L'{' || c == L'[';
    }
    return false;
}

static void AddText(Png& pn, const char* type, const ws& kw, const ws& extra, const ws& body) {
    Txt t;
    t.head = ws(type, type + 4) + L": " + kw;
    if (!extra.empty()) t.head += L" (" + extra + L")";
    t.body = body; t.json = IsJsonLike(body);
    pn.textChars += t.head.size() + t.body.size();
    pn.texts.push_back(std::move(t));
}

static ws Undecodable(size_t n) { return L"(could not decompress, " + Commas(n) + L" bytes)"; }
static ws TooLarge() { return L"(too large to decompress, > " + Commas(kInflateMax >> 20) + L" MB)"; }

static void ParseTextChunk(Png& pn, const char* type, const uint8_t* b, size_t len) {
    if (pn.textChars > kPanelMax) return;   // 情報パネルに入りきらない分は集めない（切った印はパネルの最後に付く）
    const uint8_t* z = (const uint8_t*)memchr(b, 0, len);
    if (!z) { AddText(pn, type, AutoInline(b, len), ws(), ws()); return; }
    size_t kl = z - b;
    ws kw = AutoInline(b, kl);
    const uint8_t* r = z + 1; size_t rn = len - kl - 1;
    if (!memcmp(type, "tEXt", 4)) { AddText(pn, type, kw, ws(), AutoBody(r, rn)); return; }
    if (!memcmp(type, "zTXt", 4)) {
        std::vector<uint8_t> out; bool big = false;
        if (rn >= 1 && r[0] == 0 && ZlibInflate(r + 1, rn - 1, out, kInflateMax, &big)) AddText(pn, type, kw, ws(), AutoBody(out.data(), out.size()));
        else if (big) AddText(pn, type, kw, ws(), TooLarge());
        else AddText(pn, type, kw, ws(), Undecodable(rn ? rn - 1 : 0));
        return;
    }
    // iTXt: 圧縮フラグ, 圧縮方式, 言語タグ\0, 訳したキーワード\0, 本文（UTF-8）
    if (rn < 2) { AddText(pn, type, kw, ws(), ws()); return; }
    int flag = r[0], method = r[1]; r += 2; rn -= 2;
    const uint8_t* z1 = (const uint8_t*)memchr(r, 0, rn);
    if (!z1) { AddText(pn, type, kw, ws(), ws()); return; }
    ws lang = AutoInline(r, z1 - r); size_t used = z1 - r + 1;
    const uint8_t* z2 = (const uint8_t*)memchr(r + used, 0, rn - used);
    if (!z2) { AddText(pn, type, kw, lang, ws()); return; }
    ws tr = Utf8Inline(r + used, z2 - (r + used));
    const uint8_t* body = z2 + 1; size_t bn = rn - (body - r);
    ws extra = lang; if (!tr.empty()) extra += (extra.empty() ? L"" : L" / ") + tr;
    if (flag == 0) AddText(pn, type, kw, extra, Utf8Body(body, bn));
    else {
        std::vector<uint8_t> out; bool big = false;
        if (method == 0 && ZlibInflate(body, bn, out, kInflateMax, &big)) AddText(pn, type, kw, extra, Utf8Body(out.data(), out.size()));
        else if (big) AddText(pn, type, kw, extra, TooLarge());
        else AddText(pn, type, kw, extra, Undecodable(bn));
    }
}

static void ParsePng(const std::vector<uint8_t>& d, Png& pn) {
    pn.valid = ForEachPngChunk(d.data(), d.size(), [&](const uint8_t* t, const uint8_t* b, uint32_t len) {
        std::string ty((const char*)t, 4);
        pn.chunks.push_back(ty);
        if (ty == "IHDR" && len >= 13) { pn.w = Be32(b); pn.h = Be32(b + 4); pn.depth = b[8]; pn.ctype = b[9]; pn.inter = b[12]; }
        else if (ty == "PLTE") pn.plte = (int)(len / 3);
        else if (ty == "tRNS") { pn.hasTrns = true; pn.trns.assign(b, b + len); }
        else if (ty == "sRGB") pn.srgb = true;
        else if (ty == "gAMA" && len >= 4) { pn.hasGama = true; pn.gama = Be32(b) / 100000.0; }
        else if (ty == "iCCP") { const uint8_t* z = (const uint8_t*)memchr(b, 0, len); pn.hasIccp = true; pn.iccp = AutoInline(b, z ? z - b : len); }
        else if (ty == "pHYs" && len >= 9) { pn.hasPhys = true; pn.ppx = Be32(b); pn.ppy = Be32(b + 4); pn.phyUnit = b[8]; }
        else if (ty == "acTL" && len >= 8) { pn.actl = true; pn.frames = Be32(b); pn.plays = Be32(b + 4); }
        else if (ty == "fcTL" && len >= 26) {
            unsigned dn = (b[20] << 8) | b[21], dd = (b[22] << 8) | b[23];
            pn.totalMs += dn * 1000.0 / (dd ? dd : 100);
        }
        else if (ty == "tEXt" || ty == "zTXt" || ty == "iTXt") ParseTextChunk(pn, ty.c_str(), b, len);
        return true;
    });
}

static ws PngDepthName(const Png& pn) {
    static const wchar_t* kind[] = { L"grayscale", L"", L"RGB", L"indexed", L"grayscale + alpha", L"", L"RGBA" };
    wchar_t b[64]; swprintf_s(b, L"%d-bit %s", pn.depth, (pn.ctype >= 0 && pn.ctype <= 6) ? kind[pn.ctype] : L"?");
    return b;
}

static ws PngTransparency(const Png& pn) {
    if (pn.ctype == 4 || pn.ctype == 6) return L"alpha";
    if (!pn.hasTrns) return L"none";
    if (pn.ctype == 3) {
        bool zero = false, mid = false;
        for (uint8_t a : pn.trns) { if (a == 0) zero = true; else if (a != 255) mid = true; }
        return mid ? L"alpha" : zero ? L"1-bit" : L"none";
    }
    return L"1-bit";
}

// ---------------------------------------------------------------- WIC
static ws GuidName(IWICImagingFactory* f, const WICPixelFormatGUID& g, bool& indexed) {
    indexed = false;
    wchar_t b[96];
    if (g == GUID_WICPixelFormatBlackWhite) return L"1-bit black and white";
    if (g == GUID_WICPixelFormat1bppIndexed || g == GUID_WICPixelFormat2bppIndexed || g == GUID_WICPixelFormat4bppIndexed || g == GUID_WICPixelFormat8bppIndexed) {
        indexed = true;
        int bits = g == GUID_WICPixelFormat1bppIndexed ? 1 : g == GUID_WICPixelFormat2bppIndexed ? 2 : g == GUID_WICPixelFormat4bppIndexed ? 4 : 8;
        swprintf_s(b, L"%d-bit indexed", bits); return b;
    }
    ComPtr<IWICComponentInfo> ci; ComPtr<IWICPixelFormatInfo> pi;
    UINT bpp = 0, ch = 0; wchar_t fn[64] = L""; UINT got = 0;
    if (SUCCEEDED(f->CreateComponentInfo(g, &ci)) && SUCCEEDED(ci->QueryInterface(IID_PPV_ARGS(&pi)))) {
        pi->GetBitsPerPixel(&bpp); pi->GetChannelCount(&ch); ci->GetFriendlyName(64, fn, &got);
    }
    if (!bpp || !ch) return fn[0] ? ws(fn) : ws(L"unknown");
    if (g == GUID_WICPixelFormat2bppGray || g == GUID_WICPixelFormat4bppGray || g == GUID_WICPixelFormat8bppGray || g == GUID_WICPixelFormat16bppGray ||
        g == GUID_WICPixelFormat32bppGrayFloat || g == GUID_WICPixelFormat16bppGrayFixedPoint || g == GUID_WICPixelFormat32bppGrayFixedPoint || g == GUID_WICPixelFormat16bppGrayHalf) {
        swprintf_s(b, L"%u-bit grayscale", bpp); return b;
    }
    if (bpp % ch) { swprintf_s(b, L"%ubpp %s", bpp, fn); return b; }
    bool alpha = g == GUID_WICPixelFormat32bppBGRA || g == GUID_WICPixelFormat32bppPBGRA || g == GUID_WICPixelFormat32bppRGBA || g == GUID_WICPixelFormat32bppPRGBA ||
                 g == GUID_WICPixelFormat64bppRGBA || g == GUID_WICPixelFormat64bppPRGBA || g == GUID_WICPixelFormat64bppBGRA || g == GUID_WICPixelFormat128bppRGBAFloat ||
                 g == GUID_WICPixelFormat64bppRGBAHalf || g == GUID_WICPixelFormat64bppRGBAFixedPoint || g == GUID_WICPixelFormat128bppRGBAFixedPoint;
    const wchar_t* kind = ch == 4 ? (alpha ? L"RGBA" : L"CMYK") : ch == 3 ? L"RGB" : L"";
    if (!*kind) { swprintf_s(b, L"%ubpp %s", bpp, fn); return b; }
    swprintf_s(b, L"%u-bit %s", bpp / ch, kind);
    return b;
}

// PROPVARIANT の値を文字列にする（UserComment は別扱い）
static ws HexBytes(const uint8_t* p, size_t n, size_t cap = 32) {
    ws s; wchar_t b[8];
    for (size_t i = 0; i < n && i < cap; i++) { swprintf_s(b, L"%s%02X", i ? L" " : L"", p[i]); s += b; }
    if (n > cap) s += L" ... (" + Commas(n) + L" bytes)";
    return s;
}

static ws DecodeUserComment(const uint8_t* p, size_t n, int bigEndian) {
    if (n < 8) return HexBytes(p, n);
    const uint8_t* b = p + 8; size_t bn = n - 8;
    ws r; size_t more = 0;
    if (!memcmp(p, "UNICODE\0", 8)) {
        int be = bigEndian;
        if (be < 0) be = (bn >= 2 && b[0] == 0 && b[1] != 0) ? 1 : 0;   // 不明なときは先頭の様子で決める
        size_t units = std::min(bn / 2, kTextMax); more = bn / 2 - units;
        r.resize(units);
        for (size_t i = 0; i < units; i++) r[i] = be ? ((b[2 * i] << 8) | b[2 * i + 1]) : ((b[2 * i + 1] << 8) | b[2 * i]);
    } else if (!memcmp(p, "JIS\0\0\0\0\0", 8)) {
        size_t jb = std::min(bn, kTextMax); more = bn - jb;
        int len = MultiByteToWideChar(50220, 0, (const char*)b, (int)jb, nullptr, 0);
        r.resize(len); if (len) MultiByteToWideChar(50220, 0, (const char*)b, (int)jb, &r[0], len);
    } else r = AutoToWideLim(b, bn, more);   // ASCII か、未定義（8バイト0）
    while (!r.empty() && (r.back() == 0 || r.back() == L' ')) r.pop_back();
    if (more) r += L" " + TruncMark(more);
    return r;
}

struct MetaCtx { int exifBE = -1; };

// ---- メタデータの値（バイト列はどこでも、ほぼ ASCII なら文字に読む。exif = true のときは整数 64bit を分数に読む）
static bool IsPrintableByte(uint8_t c) { return (c >= 0x20 && c <= 0x7E) || c == 9 || c == 10 || c == 13; }

// バイト列：末尾の NUL を除いて、印字できる ASCII が 90% 以上なら文字（途中の NUL は空白）、そうでなければ16進
static ws BytesText(const uint8_t* p, size_t n) {
    size_t m = n; while (m && p[m - 1] == 0) m--;
    if (m) {
        size_t ok = 0;
        for (size_t i = 0; i < m; i++) if (p[i] == 0 || IsPrintableByte(p[i])) ok++;
        if (ok * 10 >= m * 9) {
            size_t k = std::min(m, kTextMax);   // 文字に直すのは上限の範囲だけ
            ws r(k, L' '); for (size_t i = 0; i < k; i++) if (p[i]) r[i] = p[i];
            if (k < m) r += L" " + TruncMark(m - k);
            return r;
        }
    }
    return HexBytes(p, n);
}

// 分数：分母 0 は n/0、割り切れれば整数、そうでなければ小数（最大4桁、末尾の0は削る）
static ws FracText(long long n, long long d) {
    wchar_t b[64];
    if (d == 0) { swprintf_s(b, L"%lld/0", n); return b; }
    if (n % d == 0) { swprintf_s(b, L"%lld", n / d); return b; }
    swprintf_s(b, L"%.4f", (double)n / (double)d);
    ws s = b;
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    if (s == L"-0") s = L"0";
    return s;
}

// SRATIONAL を WIC が VT_UI8 で返す場合に備えて、符号付きのはずのタグは符号付きで読む
static bool SignedRationalTag(int tag) { return tag == 37377 || tag == 37379 || tag == 37380; }

// i 番目の要素が分数なら true（ベクターでないときは i = 0）
static bool GetFrac(const PROPVARIANT& v, size_t i, int tag, long long& n, long long& d) {
    bool vec = (v.vt & VT_VECTOR) != 0;
    unsigned t = v.vt & ~VT_VECTOR;
    unsigned long long q;
    if (t == VT_UI8) q = vec ? v.cauh.pElems[i].QuadPart : v.uhVal.QuadPart;
    else if (t == VT_I8) q = (unsigned long long)(vec ? v.cah.pElems[i].QuadPart : v.hVal.QuadPart);
    else return false;
    if (t == VT_I8 || SignedRationalTag(tag)) { n = (int32_t)(uint32_t)q; d = (int32_t)(uint32_t)(q >> 32); }
    else { n = (uint32_t)q; d = (uint32_t)(q >> 32); }
    return true;
}

static ws VariantText(const PROPVARIANT& pv, int tag, const MetaCtx& mc, bool exif = false) {
    wchar_t b[64];
    auto scalar = [&](const PROPVARIANT& v, size_t i) -> ws {
        switch (v.vt & ~VT_VECTOR) {
        case VT_UI1: swprintf_s(b, L"%u", v.caub.pElems[i]); break;
        case VT_I1: swprintf_s(b, L"%d", v.cac.pElems[i]); break;
        case VT_UI2: swprintf_s(b, L"%u", v.caui.pElems[i]); break;
        case VT_I2: swprintf_s(b, L"%d", v.cai.pElems[i]); break;
        case VT_UI4: swprintf_s(b, L"%lu", v.caul.pElems[i]); break;
        case VT_I4: case VT_INT: swprintf_s(b, L"%ld", v.cal.pElems[i]); break;
        case VT_UI8: case VT_I8: {
            long long n, d;
            if (exif && GetFrac(v, i, tag, n, d)) return FracText(n, d);
            if ((v.vt & ~VT_VECTOR) == VT_UI8) swprintf_s(b, L"%llu", v.cauh.pElems[i].QuadPart); else swprintf_s(b, L"%lld", v.cah.pElems[i].QuadPart);
            break;
        }
        case VT_R4: swprintf_s(b, L"%g", v.caflt.pElems[i]); break;
        case VT_R8: swprintf_s(b, L"%g", v.cadbl.pElems[i]); break;
        case VT_LPSTR: return v.calpstr.pElems[i] ? AutoInline((const uint8_t*)v.calpstr.pElems[i], strlen(v.calpstr.pElems[i])) : ws();
        case VT_LPWSTR: return v.calpwstr.pElems[i] ? ws(v.calpwstr.pElems[i]) : ws();
        case VT_BSTR: return v.cabstr.pElems[i] ? ws(v.cabstr.pElems[i], SysStringLen(v.cabstr.pElems[i])) : ws();
        case VT_BOOL: return v.cabool.pElems[i] ? L"true" : L"false";
        default: swprintf_s(b, L"(type %u)", (unsigned)v.vt);
        }
        return b;
    };
    if (pv.vt & VT_VECTOR) {
        ULONG n = pv.caub.cElems;
        if ((pv.vt & ~VT_VECTOR) == VT_UI1) {
            if (tag == 37510) return DecodeUserComment(pv.caub.pElems, n, mc.exifBE);
            return BytesText(pv.caub.pElems, n);
        }
        ws s; ULONG lim = std::min<ULONG>(n, 16);
        for (ULONG i = 0; i < lim; i++) { if (i) s += L", "; s += scalar(pv, i); }
        if (n > lim) s += L", ... (" + Commas(n) + L" values)";
        return s;
    }
    switch (pv.vt) {
    case VT_LPSTR: return pv.pszVal ? AutoInline((const uint8_t*)pv.pszVal, strlen(pv.pszVal)) : ws();
    case VT_LPWSTR: return pv.pwszVal ? ws(pv.pwszVal) : ws();
    case VT_BSTR: return pv.bstrVal ? ws(pv.bstrVal, SysStringLen(pv.bstrVal)) : ws();
    case VT_BOOL: return pv.boolVal ? L"true" : L"false";
    case VT_UI1: swprintf_s(b, L"%u", pv.bVal); return b;
    case VT_I1: swprintf_s(b, L"%d", pv.cVal); return b;
    case VT_UI2: swprintf_s(b, L"%u", pv.uiVal); return b;
    case VT_I2: swprintf_s(b, L"%d", pv.iVal); return b;
    case VT_UI4: case VT_UINT: swprintf_s(b, L"%lu", pv.ulVal); return b;
    case VT_I4: case VT_INT: swprintf_s(b, L"%ld", pv.lVal); return b;
    case VT_UI8: case VT_I8: {
        long long n, d;
        if (exif && GetFrac(pv, 0, tag, n, d)) return FracText(n, d);
        if (pv.vt == VT_UI8) swprintf_s(b, L"%llu", pv.uhVal.QuadPart); else swprintf_s(b, L"%lld", pv.hVal.QuadPart);
        return b;
    }
    case VT_R4: swprintf_s(b, L"%g", pv.fltVal); return b;
    case VT_R8: swprintf_s(b, L"%g", pv.dblVal); return b;
    case VT_CLSID: if (pv.puuid) { StringFromGUID2(*pv.puuid, b, 64); return b; } break;
    case VT_BLOB: return BytesText(pv.blob.pBlobData, pv.blob.cbSize);
    }
    swprintf_s(b, L"(type %u)", (unsigned)pv.vt);
    return b;
}

static const wchar_t* OrientName(int o) {
    static const wchar_t* const nm[9] = { L"", L"", L"flipped horizontally", L"rotated 180°", L"flipped vertically", L"transposed", L"rotated 90° CW", L"transversed", L"rotated 90° CCW" };
    return o >= 2 && o <= 8 ? nm[o] : L"";
}

// 整数値（スカラー）として読めるか
static bool GetInt(const PROPVARIANT& pv, long long& v) {
    switch (pv.vt) {
    case VT_UI1: v = pv.bVal; return true;
    case VT_UI2: v = pv.uiVal; return true;
    case VT_I2: v = pv.iVal; return true;
    case VT_UI4: case VT_UINT: v = pv.ulVal; return true;
    case VT_I4: case VT_INT: v = pv.lVal; return true;
    }
    return false;
}

// 3つの分数（度分秒・時分秒）を取る
static bool Get3(const PROPVARIANT& pv, int tag, double out[3], long long nd[3][2]) {
    if (pv.vt != (VT_VECTOR | VT_UI8) && pv.vt != (VT_VECTOR | VT_I8)) return false;
    if (pv.cauh.cElems != 3) return false;
    for (int i = 0; i < 3; i++) {
        if (!GetFrac(pv, i, tag, nd[i][0], nd[i][1]) || nd[i][1] == 0) return false;
        out[i] = (double)nd[i][0] / (double)nd[i][1];
    }
    return true;
}

// EXIF / TIFF の IFD から来る1タグの値
static ws ExifValue(const PROPVARIANT& pv, ExifGroup g, int tag, const MetaCtx& mc) {
    long long n, d, iv;
    wchar_t b[96];
    if (g == EG_EXIF && !(pv.vt & VT_VECTOR) && GetFrac(pv, 0, tag, n, d)) {
        switch (tag) {
        case 33434:   // ExposureTime
            if (d == 0) break;
            if (n == 0) return L"0 s";
            if (n > 0 && n < d) { swprintf_s(b, L"1/%lld s", (long long)std::llround((double)d / (double)n)); return b; }
            return FracText(n, d) + L" s";
        case 33437: return L"f/" + FracText(n, d);   // FNumber
        case 37386: return FracText(n, d) + L" mm";   // FocalLength
        case 37380: {   // ExposureBiasValue
            ws t = FracText(n, d);
            if (d != 0 && t != L"0" && t[0] != L'-') t = L"+" + t;
            return t + L" EV";
        }
        }
    }
    if (g == EG_GPS && (tag == 2 || tag == 4)) {
        double v[3]; long long nd[3][2];
        if (Get3(pv, tag, v, nd)) return FracText(nd[0][0], nd[0][1]) + L"° " + FracText(nd[1][0], nd[1][1]) + L"' " + FracText(nd[2][0], nd[2][1]) + L"\"";
    }
    if (g == EG_GPS && tag == 7) {
        double v[3]; long long nd[3][2];
        if (Get3(pv, tag, v, nd)) {
            ws sec = FracText((long long)std::llround(v[2] * 100), 100);   // 小数は2桁まで
            if (sec.size() < 2 || sec[1] == L'.') sec = L"0" + sec;
            swprintf_s(b, L"%02d:%02d:", (int)v[0], (int)v[1]);
            return b + sec;
        }
    }
    if (!(pv.vt & VT_VECTOR) && GetInt(pv, iv)) {
        if ((g == EG_IFD0 || g == EG_THUMB) && tag == 296 && (iv == 2 || iv == 3)) return iv == 2 ? L"inch" : L"cm";
        if (g == EG_EXIF && tag == 40961 && (iv == 1 || iv == 65535)) return iv == 1 ? L"sRGB" : L"Uncalibrated";
        if ((g == EG_IFD0 || g == EG_THUMB) && tag == 274 && iv >= 2 && iv <= 8) { swprintf_s(b, L"%lld (%s)", iv, OrientName((int)iv)); return b; }
    }
    return VariantText(pv, tag, mc, true);
}

static const struct { int tag; const wchar_t* name; } kTags[] = {
    { 271, L"Make" }, { 272, L"Model" }, { 36867, L"DateTimeOriginal" }, { 305, L"Software" }, { 270, L"ImageDescription" }, { 37510, L"UserComment" },
};

// "{ushort=271}" で終わる名前からタグ番号を取る
static int TagOf(const ws& name) {
    size_t i = name.rfind(L"{ushort=");
    if (i == ws::npos) return -1;
    return _wtoi(name.c_str() + i + 8);
}

// WIC が列挙する {ushort=0} 形式の IFD 名を、クエリでふつうに使う ifd / exif / gps / thumb に直す
static ws FriendlyPath(ws p) {
    static const wchar_t* map[][2] = { { L"/app1/{ushort=0}", L"/app1/ifd" }, { L"/app1/{ushort=1}", L"/app1/thumb" },
        { L"/ifd/{ushort=34665}", L"/ifd/exif" }, { L"/ifd/{ushort=34853}", L"/ifd/gps" }, { L"/exif/{ushort=40965}", L"/exif/interop" } };
    for (auto& m : map) { size_t i = p.find(m[0]); if (i != ws::npos) p.replace(i, wcslen(m[0]), m[1]); }
    return p;
}

// EXIF / TIFF の IFD から来たタグなら、グループとタグ番号を返す（FriendlyPath 済みの "/app1/ifd/exif/{ushort=33434}" など）
static bool ClassifyExif(const ws& path, ExifGroup& g, int& tag) {
    size_t i = path.rfind(L"/{ushort=");
    if (i == ws::npos || i == 0 || path.back() != L'}') return false;
    wchar_t* end = nullptr;
    long t = wcstol(path.c_str() + i + 9, &end, 10);
    if (end == path.c_str() + i + 9 || *end != L'}') return false;
    ws last;
    for (size_t pos = 0; pos < i;) {   // コンテナの各区切りが IFD 系の名前だけであること
        size_t q = path.find(L'/', pos + 1); if (q == ws::npos || q > i) q = i;
        ws seg = path.substr(pos + 1, q - pos - 1);
        if (seg != L"app1" && seg != L"ifd" && seg != L"exif" && seg != L"gps" && seg != L"interop" && seg != L"thumb") return false;
        last = seg; pos = q;
    }
    if (last == L"ifd") g = EG_IFD0; else if (last == L"exif") g = EG_EXIF; else if (last == L"gps") g = EG_GPS;
    else if (last == L"interop") g = EG_INTEROP; else if (last == L"thumb") g = EG_THUMB; else return false;
    tag = (int)t;
    return true;
}

// "名前: 値" の名前。サムネイル（IFD1）は Thumbnail、Interop は Interop を前に付ける。表に無ければ Tag 0x8822
static ws ExifName(ExifGroup g, int tag) {
    ws pre = g == EG_THUMB ? L"Thumbnail " : g == EG_INTEROP ? L"Interop " : g == EG_GPS ? L"GPS " : L"";
    if (const wchar_t* n = ExifTagName(g, tag)) return (g == EG_GPS ? ws() : pre) + n;
    wchar_t b[24]; swprintf_s(b, L"Tag 0x%04X", tag);
    return pre + b;
}

static void DumpReader(IWICMetadataQueryReader* r, const ws& prefix, std::vector<ws>& lines, std::set<ws>& seen, const MetaCtx& mc, int depth) {
    if (!r || depth > 8) return;
    ComPtr<IEnumString> en;
    if (FAILED(r->GetEnumerator(&en))) return;
    LPOLESTR nm; ULONG got = 0;
    while (en->Next(1, &nm, &got) == S_OK && got == 1) {
        ws name = nm; CoTaskMemFree(nm);
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(r->GetMetadataByName(name.c_str(), &pv))) {
            if (pv.vt == VT_UNKNOWN && pv.punkVal) {
                ComPtr<IWICMetadataQueryReader> sub;
                if (SUCCEEDED(pv.punkVal->QueryInterface(IID_PPV_ARGS(&sub)))) DumpReader(sub.Get(), prefix + name, lines, seen, mc, depth + 1);
            } else {
                int tag = TagOf(name);
                ws path = FriendlyPath(prefix + name);
                ExifGroup eg; int et;
                if (ClassifyExif(path, eg, et)) {
                    // IFD を指すだけのタグは出さない
                    if (et != 34665 && et != 34853 && et != 40965 && seen.insert(path).second)
                        lines.push_back(CapLine(ExifName(eg, et) + L": " + ExifValue(pv, eg, et, mc)));
                } else {
                    ws head = path;
                    for (auto& t : kTags) if (t.tag == tag) { head += L" ("; head += t.name; head += L")"; }
                    ws l = CapLine(head + L": " + VariantText(pv, tag, mc));
                    if (seen.insert(path).second) lines.push_back(l);
                }
            }
        }
        PropVariantClear(&pv);
    }
}

// ---------------------------------------------------------------- JPEG / WebP の生の XMP と EXIF のバイト順
static void ScanContainer(const std::vector<uint8_t>& d, ws& xmp, MetaCtx& mc) {
    static const char xap[] = "http://ns.adobe.com/xap/1.0/";
    auto tiffOrder = [&](const uint8_t* t, size_t n) { if (n >= 4 && t[0] == 'M' && t[1] == 'M') mc.exifBE = 1; else if (n >= 4 && t[0] == 'I' && t[1] == 'I') mc.exifBE = 0; };
    bool jpeg = ForEachJpegSegment(d.data(), d.size(), [&](uint8_t m, const uint8_t* b, size_t bn) {
        if (m == 0xE1) {
            if (bn > sizeof xap && !memcmp(b, xap, sizeof xap - 1) && b[sizeof xap - 1] == 0 && xmp.empty()) {
                size_t off = sizeof xap; xmp = Utf8Body(b + off, bn - off);
            } else if (bn > 6 && !memcmp(b, "Exif\0\0", 6)) tiffOrder(b + 6, bn - 6);
        }
        return true;
    });
    if (!jpeg) ForEachWebpChunk(d.data(), d.size(), [&](const uint8_t* t, const uint8_t* b, size_t len) {
        if (!memcmp(t, "XMP ", 4) && xmp.empty()) xmp = Utf8Body(b, len);
        else if (!memcmp(t, "EXIF", 4)) { SkipExifHeader(b, len); tiffOrder(b, len); }
        return true;
    });
    while (!xmp.empty() && (xmp.back() == 0)) xmp.pop_back();
}

// ---------------------------------------------------------------- 色数
// 色数・透明の種類のための展開。本体と同じ kMemLimit をかけ、超えるときは展開せず false（tooLarge を立てる）
static bool DecodeBGRA(IWICImagingFactory* f, const std::vector<uint8_t>& data, std::vector<uint32_t>& px, bool& tooLarge) {
    tooLarge = false;
    ComPtr<IStream> s = MemStream(data.data(), data.size());
    if (!s) return false;
    ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr; ComPtr<IWICFormatConverter> cv;
    UINT w = 0, h = 0; bool ok = false;
    if (SUCCEEDED(f->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(fr->GetSize(&w, &h)) && w && h && w <= 32768 && h <= 32768) {
        if ((unsigned long long)w * h * 4 > kMemLimit) { tooLarge = true; return false; }
        if (SUCCEEDED(f->CreateFormatConverter(&cv)) &&
            SUCCEEDED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            try { px.assign((size_t)w * h, 0); ok = SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)(px.size() * 4), (BYTE*)px.data())); } catch (...) {}
        }
    }
    return ok;
}

// RGBA の種類数。cancel が立ったら -1
static long long CountColors(const std::vector<uint32_t>& px, const std::atomic<bool>& cancel) {
    int bits = 12; size_t cap = (size_t)1 << bits, used = 0;
    std::vector<uint32_t> tab(cap, 0);
    bool hasZero = false;
    auto put = [&](std::vector<uint32_t>& t, int b, uint32_t v) -> bool {
        size_t mask = ((size_t)1 << b) - 1, i = (uint32_t)(v * 0x9E3779B1u) >> (32 - b);
        while (t[i]) { if (t[i] == v) return false; i = (i + 1) & mask; }
        t[i] = v; return true;
    };
    for (size_t k = 0; k < px.size(); k++) {
        if ((k & 0xFFFF) == 0 && cancel.load()) return -1;
        uint32_t v = px[k];
        if (v == 0) { hasZero = true; continue; }
        if (put(tab, bits, v)) {
            used++;
            if (used * 2 > cap) {   // 倍に広げて入れ直す
                std::vector<uint32_t> nt((size_t)1 << (bits + 1), 0);
                for (uint32_t u : tab) if (u) put(nt, bits + 1, u);
                tab.swap(nt); bits++; cap <<= 1;
            }
        }
    }
    return (long long)used + (hasZero ? 1 : 0);
}

static ws AlphaKind(const std::vector<uint32_t>& px) {
    bool partial = false, clear = false;
    for (uint32_t v : px) { uint32_t a = v >> 24; if (a == 0) clear = true; else if (a != 255) { partial = true; break; } }
    return partial ? L"alpha" : clear ? L"1-bit" : L"none";
}

// ---------------------------------------------------------------- ファイル
static ws FormatOf(const std::vector<uint8_t>& d, const ws& path, const Png& pn) {
    if (pn.valid) return pn.actl ? L"APNG" : L"PNG";
    if (d.size() > 2 && d[0] == 0xFF && d[1] == 0xD8) return L"JPEG";
    if (d.size() > 6 && !memcmp(d.data(), "GIF8", 4)) return L"GIF";
    if (d.size() > 2 && d[0] == 'B' && d[1] == 'M') return L"BMP";
    if (d.size() > 12 && !memcmp(d.data(), "RIFF", 4) && !memcmp(&d[8], "WEBP", 4)) return L"WebP";
    if (d.size() > 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0) return L"ICO";
    const wchar_t* e = PathFindExtensionW(path.c_str());
    if (!_wcsicmp(e, L".svg")) return L"SVG";
    ws u = *e ? e + 1 : L""; for (auto& c : u) c = towupper(c);
    return u.empty() ? ws(L"unknown") : u;
}

static ws SizeText(ULONGLONG b) {
    ws s = Commas(b) + L" bytes"; wchar_t t[64];
    if (b >= (1ull << 30)) swprintf_s(t, L" (%.2f GB)", b / 1073741824.0);
    else if (b >= (1ull << 20)) swprintf_s(t, L" (%.2f MB)", b / 1048576.0);
    else if (b >= 1024) swprintf_s(t, L" (%.2f KB)", b / 1024.0);
    else t[0] = 0;
    return s + t;
}

static ws Ratio(unsigned w, unsigned h) {
    unsigned a = w, b = h; while (b) { unsigned t = a % b; a = b; b = t; }
    if (!a) a = 1;
    wchar_t t[96]; swprintf_s(t, L"%u:%u (%.3f)", w / a, h / a, h ? (double)w / h : 0.0);
    return t;
}

static ws DurationText(double ms) { wchar_t b[32]; swprintf_s(b, L"%.2f s", ms / 1000.0); return b; }

static ws LoopText(unsigned plays) {
    if (plays == 0) return L"loop forever";
    if (plays == 1) return L"play once";
    return L"loop " + Commas(plays) + L" times";
}

// 空白・タブ・改行をはさまない文字の連なりの最大の長さ
static size_t LongestRun(const ws& t) {
    size_t best = 0, run = 0;
    for (wchar_t c : t) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') run = 0;
        else if (++run > best) best = run;
    }
    return best;
}

// 改行を CRLF にそろえ、NUL を空白に置き換える。off の位置は変換後の位置に直して返す
static ws ToCrlf(const ws& s, int& off) {
    ws r; r.reserve(s.size() + s.size() / 20 + 16);
    int mapped = -1;
    for (size_t i = 0; i <= s.size(); i++) {
        if ((int)i == off) mapped = (int)r.size();
        if (i == s.size()) break;
        wchar_t c = s[i];
        if (c == L'\r') { if (i + 1 < s.size() && s[i + 1] == L'\n') { if ((int)i + 1 == off) off++; i++; } r += L"\r\n"; }
        else if (c == L'\n') r += L"\r\n";
        else if (c == 0) r += L' ';
        else r += c;
    }
    off = mapped;
    return r;
}

static ws ResText(double dx, double dy) {
    auto f1 = [](double v) { wchar_t t[32]; if (std::fabs(v - std::round(v)) < 0.05) swprintf_s(t, L"%.0f", v); else swprintf_s(t, L"%.2f", v); return ws(t); };
    return f1(dx) + L" x " + f1(dy) + L" dpi";
}

// メモリ上限・コマ数の上限で途中までしか展開していないときの注記（2つは別の注記）
static int FramesInFile(const InfoReq& rq) { return (rq.memLimited || rq.frameLimited) && rq.framesTotal > rq.frames ? rq.framesTotal : rq.frames; }
static ws LimitText(const InfoReq& rq) {
    if (rq.memLimited) return L" (showing first " + Commas(rq.frames) + L", memory limit " + std::to_wstring(kMemLimitGB) + L" GB)";
    if (rq.frameLimited) return L" (showing first " + Commas(rq.frames) + L", frame limit)";
    return L"";
}

static void Job(const InfoReq& rq, const std::atomic<bool>& cancel, const InfoTextFn& onText, const InfoColorsFn& onColors) {
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    ws s;
    std::vector<uint8_t> data;
    bool readOk = ReadFileAll(rq.path, data);
    if (cancel) return;

    Png pn; if (readOk) ParsePng(data, pn);
    WIN32_FILE_ATTRIBUTE_DATA fa; bool st = GetFileAttributesExW(rq.path.c_str(), GetFileExInfoStandard, &fa) != 0;

    s += L"[File]\n";
    Line(s, L"Name", NameOf(rq.path));
    Line(s, L"Folder", DirOf(rq.path));
    if (st) {
        Line(s, L"Size", SizeText(((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow));
        FILETIME lft; SYSTEMTIME sy;
        if (FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lft) && FileTimeToSystemTime(&lft, &sy))
            LineF(s, L"Modified", L"%04d-%02d-%02d %02d:%02d:%02d", sy.wYear, sy.wMonth, sy.wDay, sy.wHour, sy.wMinute, sy.wSecond);
    }
    if (readOk) Line(s, L"Format", FormatOf(data, rq.path, pn));

    bool svg = rq.svg || (!_wcsicmp(PathFindExtensionW(rq.path.c_str()), L".svg"));
    unsigned iw = rq.w, ih = rq.h;
    if (pn.valid && pn.w && pn.h) { iw = pn.w; ih = pn.h; if (rq.orient >= 5) std::swap(iw, ih); }   // ファイルの値は回転前。回転後を出す
    bool haveImage = svg ? (rq.decoded && iw) : ((pn.valid && pn.w) || (rq.decoded && readOk));

    ws xmp; MetaCtx mc;
    std::vector<ws> metaLines;
    std::vector<uint32_t> px; bool pxOk = false, pxBig = false;
    size_t colorsOff = ws::npos;
    bool needCount = false;

    if (haveImage) {
        s += L"\n[Image]\n";
        Line(s, L"Dimensions", Commas(iw) + L" x " + Commas(ih));
        if (!svg && rq.orient >= 2 && rq.orient <= 8) {
            LineF(s, L"Orientation", L"%d (%s)", rq.orient, OrientName(rq.orient));
        }
        Line(s, L"Aspect", Ratio(iw, ih));
        if (!svg) {
            ws late;   // Colors used より後ろに並べる行
            Line(s, L"Pixels", Commas((unsigned long long)iw * ih));
            if (pn.valid) {
                Line(s, L"Bit depth", PngDepthName(pn));
                if (pn.inter == 1) Line(s, L"Interlace", L"Adam7");
                if (pn.plte >= 0) Line(s, L"Palette", Commas(pn.plte) + (pn.plte == 1 ? L" entry" : L" entries"));
                Line(s, L"Transparency", PngTransparency(pn));
                if (pn.actl) LineF(late, L"Frames", L"%lu (%s, %s)%s", pn.frames, DurationText(pn.totalMs).c_str(), LoopText(pn.plays).c_str(), LimitText(rq).c_str());
                if (pn.hasPhys && pn.phyUnit == 1 && pn.ppx && pn.ppy) Line(late, L"Resolution", ResText(pn.ppx * 0.0254, pn.ppy * 0.0254));
                ws cs;
                auto add = [&](const ws& v) { if (!cs.empty()) cs += L", "; cs += v; };
                if (pn.srgb) add(L"sRGB");
                if (pn.hasGama) { wchar_t b[48]; swprintf_s(b, L"gAMA %.5f", pn.gama); add(b); }
                if (pn.hasIccp) add(L"iCCP \"" + pn.iccp + L"\"");
                if (!cs.empty()) Line(late, L"Color space", cs);
                ws ch;
                for (size_t i = 0; i < pn.chunks.size();) {
                    size_t j = i; while (j < pn.chunks.size() && pn.chunks[j] == pn.chunks[i]) j++;
                    if (!ch.empty()) ch += L", ";
                    ch += ws(pn.chunks[i].begin(), pn.chunks[i].end());
                    if (j - i > 1) ch += L" x" + Commas(j - i);
                    i = j;
                }
                Line(late, L"Chunks", ch);
            } else if (wic.Get() && readOk) {
                // WIC のピクセル形式・パレット・解像度・メタデータ
                ComPtr<IStream> ms = MemStream(data.data(), data.size());
                ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr;
                if (ms && SUCCEEDED(wic->CreateDecoderFromStream(ms.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr))) {
                    WICPixelFormatGUID pf{}; fr->GetPixelFormat(&pf);
                    bool indexed; Line(s, L"Bit depth", GuidName(wic.Get(), pf, indexed));
                    if (indexed) {
                        ComPtr<IWICPalette> pal; UINT n = 0;
                        if (SUCCEEDED(wic->CreatePalette(&pal)) && SUCCEEDED(fr->CopyPalette(pal.Get())) && SUCCEEDED(pal->GetColorCount(&n)) && n)
                            Line(s, L"Palette", Commas(n) + (n == 1 ? L" entry" : L" entries"));
                    }
                    ScanContainer(data, xmp, mc);
                    if (cancel) return;   // ピクセル展開の前
                    // 透明の種類は実際のピクセルから（色数のための展開と共用する）
                    pxOk = DecodeBGRA(wic.Get(), data, px, pxBig);
                    Line(s, L"Transparency", pxOk ? AlphaKind(px) : ws(L"unknown"));
                    std::set<ws> seen;
                    ComPtr<IWICMetadataQueryReader> dq, fq;
                    if (SUCCEEDED(dec->GetMetadataQueryReader(&dq))) DumpReader(dq.Get(), L"", metaLines, seen, mc, 0);
                    if (cancel) return;   // メタデータ（本体の分）を読んだ後
                    if (SUCCEEDED(fr->GetMetadataQueryReader(&fq))) {
                        DumpReader(fq.Get(), L"", metaLines, seen, mc, 0);
                        PROPVARIANT pv; PropVariantInit(&pv);
                        if (!seen.count(L"/commentext/TextEntry") && SUCCEEDED(fq->GetMetadataByName(L"/commentext/TextEntry", &pv)) && pv.vt == VT_LPWSTR)
                            metaLines.push_back(CapLine(L"/commentext/TextEntry: " + VariantText(pv, -1, mc)));
                        PropVariantClear(&pv);
                    }
                    if (cancel) return;   // メタデータ（フレームの分）を読んだ後
                    if (rq.frames > 1 && dq) {   // GIF のループ回数
                        PROPVARIANT lv; PropVariantInit(&lv); unsigned plays = 1;
                        if (SUCCEEDED(dq->GetMetadataByName(L"/appext/Data", &lv)) && lv.vt == (VT_VECTOR | VT_UI1) && lv.caub.cElems >= 3) {
                            const uint8_t* d = lv.caub.pElems; ULONG n = lv.caub.cElems;
                            if (n >= 4 && d[0] == 3 && d[1] == 1) plays = d[2] | (d[3] << 8);
                            else if (d[0] == 1) plays = d[1] | (d[2] << 8);
                        }
                        PropVariantClear(&lv);
                        if (FormatOf(data, rq.path, pn) == L"GIF") {
                            // 途中までしか展開していないときは、全体の長さが分からないので時間は出さない
                            if (rq.memLimited || rq.frameLimited) LineF(late, L"Frames", L"%d (%s)%s", FramesInFile(rq), LoopText(plays).c_str(), LimitText(rq).c_str());
                            else LineF(late, L"Frames", L"%d (%s, %s)", rq.frames, DurationText((double)rq.totalMs).c_str(), LoopText(plays).c_str());
                        }
                        else LineF(late, L"Frames", L"%d%s", FramesInFile(rq), LimitText(rq).c_str());
                    } else if (rq.frames > 1) LineF(late, L"Frames", L"%d%s", FramesInFile(rq), LimitText(rq).c_str());
                    double dx = 0, dy = 0;
                    if (SUCCEEDED(fr->GetResolution(&dx, &dy)) && dx > 0 && dy > 0) Line(late, L"Resolution", ResText(dx, dy));
                }
            }
            s += L"Colors used: ";
            colorsOff = s.size();
            s += L"counting...\n";
            needCount = true;
            s += late;
        }
    }

    if (!xmp.empty())   // 生の XML を出すので、WIC が分解した /xmp の行は出さない
        metaLines.erase(std::remove_if(metaLines.begin(), metaLines.end(), [](const ws& l) { return l.compare(0, 5, L"/xmp/") == 0; }), metaLines.end());

    // 埋め込みテキスト。{ か [ で始まるものは最後にまとめて出す
    if (pn.valid) {
        for (int pass = 0; pass < 2; pass++)
            for (auto& t : pn.texts) {
                if (s.size() > kPanelMax) break;   // パネルに入りきらない分は組み立てない
                if (t.json == (pass == 1)) { s += L"\n" + t.head + L"\n" + t.body + L"\n"; }
            }
    } else if (!metaLines.empty()) {
        s += L"\n[Metadata]\n";
        for (auto& l : metaLines) { if (s.size() > kPanelMax) break; s += l; s += L'\n'; }
    }
    if (!xmp.empty() && s.size() <= kPanelMax) s += L"\nXMP:\n" + xmp + L"\n";
    while (!s.empty() && s.back() == L'\n') s.pop_back();

    if (cancel) return;
    InfoResult res;
    int off = colorsOff == ws::npos ? -1 : (int)colorsOff;
    res.text = ToCrlf(s, off);
    res.colorsOff = off; res.colorsLen = needCount ? 11 : 0;
    // パネル全体の上限。CRLF に直したあとの文字数で切る（CR と LF の間・サロゲートペアの途中では切らない）。
    // Colors used の位置は先頭側にあるので、切ってもずれない（切った後ろに行ってしまうときだけ、差し替えをやめる）
    if (res.text.size() > kPanelMax) {
        size_t cut = std::min(res.text.size(), kPanelMax);
        if (cut > 0 && (res.text[cut - 1] == L'\r' || (res.text[cut - 1] >= 0xD800 && res.text[cut - 1] <= 0xDBFF))) cut--;
        res.text.resize(cut);
        res.text += L"\r\n... (truncated)";
        if (res.colorsOff >= 0 && (size_t)res.colorsOff + 11 > cut) { res.colorsOff = -1; res.colorsLen = 0; needCount = false; }
    }
    res.noWrap = res.text.size() > kWrapMaxChars || LongestRun(res.text) > kWrapMaxRun;
    onText(std::move(res));

    if (needCount) {
        ws val;
        if (!pxOk && !pxBig && wic.Get() && readOk) {
            if (cancel) return;   // ピクセル展開の前
            pxOk = DecodeBGRA(wic.Get(), data, px, pxBig);
        }
        if (pxOk) {
            long long n = CountColors(px, cancel);
            if (n < 0) return;
            val = Commas((unsigned long long)n);
        } else val = pxBig ? L"(too large)" : L"(unavailable)";
        if (!cancel) onColors(std::move(val));
    }
}

}  // namespace

void RunInfoJob(const InfoReq& rq, std::shared_ptr<std::atomic<bool>> cancel, InfoTextFn onText, InfoColorsFn onColors) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        Job(rq, *cancel, onText, onColors);
    } catch (const std::bad_alloc&) {   // メモリ不足は作業の全体で受け止め、パネルにはこれだけを出す
        if (!cancel->load()) {
            InfoResult r; r.text = L"(not enough memory)";
            try { onText(std::move(r)); } catch (...) {}
        }
    }
    CoUninitialize();
}
