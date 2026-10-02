// Based on puff.c by Mark Adler (zlib license). Modified.
#include "inflate.h"

namespace {

struct Bits {
    const uint8_t* p; size_t n, pos = 0;
    uint32_t buf = 0; int cnt = 0; bool eof = false;
    int Get(int need) {
        while (cnt < need) {
            if (pos >= n) { eof = true; return 0; }
            buf |= (uint32_t)p[pos++] << cnt; cnt += 8;
        }
        int v = (int)(buf & ((1u << need) - 1)); buf >>= need; cnt -= need;
        return v;
    }
};

struct Huff { uint16_t count[16]; uint16_t symbol[288]; };

// 符号長の並びから正準ハフマン表を作る。戻り値：0 完全 / 負 過剰 / 正 不完全
static int Build(Huff& h, const uint8_t* len, int n) {
    for (int i = 0; i < 16; i++) h.count[i] = 0;
    for (int i = 0; i < n; i++) h.count[len[i]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h.count[l]; if (left < 0) return left; }
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; i++) if (len[i]) h.symbol[offs[len[i]]++] = (uint16_t)i;
    return left;
}

static int Decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= b.Get(1);
        if (b.eof) return -1;
        int c = h.count[l];
        if (code - c < first) return h.symbol[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
    }
    return -1;
}

static const uint16_t kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint16_t kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint16_t kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

// 固定ハフマン表（RFC 1951 3.2.6）。関数内 static の初期化子で作る（同時に呼ばれても安全）
struct FixedTables { Huff lc, dc; };
static const FixedTables& Fixed() {
    static const FixedTables t = [] {
        FixedTables f;
        uint8_t l[288]; int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        Build(f.lc, l, 288);
        uint8_t d[30]; for (i = 0; i < 30; i++) d[i] = 5;
        Build(f.dc, d, 30);
        return f;
    }();
    return t;
}

static bool Codes(Bits& b, std::vector<uint8_t>& out, size_t maxOut, bool& over, const Huff& lc, const Huff& dc) {
    for (;;) {
        int s = Decode(b, lc);
        if (s < 0) return false;
        if (s < 256) { if (out.size() >= maxOut) { over = true; return false; } out.push_back((uint8_t)s); }
        else if (s == 256) return true;
        else {
            s -= 257; if (s >= 29) return false;
            int len = kLenBase[s] + b.Get(kLenExtra[s]);
            int ds = Decode(b, dc);
            if (ds < 0 || ds >= 30) return false;
            size_t dist = kDistBase[ds] + b.Get(kDistExtra[ds]);
            if (b.eof || dist > out.size()) return false;
            if (out.size() + len > maxOut) { over = true; return false; }
            size_t from = out.size() - dist;
            for (int i = 0; i < len; i++) out.push_back(out[from + i]);   // 重なるコピーなので1バイトずつ
        }
    }
}

}  // namespace

static bool ZlibInflateImpl(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t maxOut, bool& over) {
    if (n < 2) return false;
    if ((src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 != 0 || (src[1] & 0x20)) return false;
    Bits b{ src + 2, n - 2 };
    int last;
    do {
        last = b.Get(1);
        int type = b.Get(2);
        if (b.eof) return false;
        if (type == 0) {
            b.buf = 0; b.cnt = 0;   // バイト境界へ
            if (b.pos + 4 > b.n) return false;
            unsigned len = b.p[b.pos] | (b.p[b.pos + 1] << 8), nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xFFFF) != nlen || b.pos + len > b.n) return false;
            if (out.size() + len > maxOut) { over = true; return false; }
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
        } else if (type == 1) {
            const FixedTables& ft = Fixed();
            if (!Codes(b, out, maxOut, over, ft.lc, ft.dc)) return false;
        } else if (type == 2) {
            int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
            if (b.eof || nlen > 286 || ndist > 30) return false;
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t lens[320] = {};
            for (int i = 0; i < ncode; i++) lens[order[i]] = (uint8_t)b.Get(3);
            Huff cl; if (Build(cl, lens, 19) != 0) return false;
            int idx = 0;
            while (idx < nlen + ndist) {
                int s = Decode(b, cl);
                if (s < 0) return false;
                if (s < 16) lens[idx++] = (uint8_t)s;
                else {
                    int rep, v = 0;
                    if (s == 16) { if (idx == 0) return false; v = lens[idx - 1]; rep = 3 + b.Get(2); }
                    else if (s == 17) rep = 3 + b.Get(3);
                    else rep = 11 + b.Get(7);
                    if (idx + rep > nlen + ndist) return false;
                    while (rep--) lens[idx++] = (uint8_t)v;
                }
            }
            if (b.eof || lens[256] == 0) return false;
            Huff lc, dc;
            int e = Build(lc, lens, nlen);
            if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return false;
            e = Build(dc, lens + nlen, ndist);
            if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return false;
            if (!Codes(b, out, maxOut, over, lc, dc)) return false;
        } else return false;
        if (b.eof) return false;
    } while (!last);
    return true;
}

bool ZlibInflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t maxOut, bool* tooLarge) {
    out.clear();
    bool over = false;
    if (tooLarge) *tooLarge = false;
    bool ok = ZlibInflateImpl(src, n, out, maxOut, over);
    if (!ok && over && tooLarge) *tooLarge = true;
    return ok;
}
