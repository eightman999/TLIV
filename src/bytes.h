#pragma once
// バイト列の読みと、画像コンテナ（PNG / JPEG / WebP）の走査。decode.cpp と meta.cpp が共有する
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cctype>

inline uint32_t Be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
inline bool HasPngSig(const uint8_t* d, size_t n) { return n >= 8 && !memcmp(d, kPngSig, 8); }

// 先頭の "Exif\0\0" が付いていれば飛ばす（付いていなければそのまま TIFF として扱う）
inline void SkipExifHeader(const uint8_t*& p, size_t& n) {
    if (n >= 6 && !memcmp(p, "Exif\0\0", 6)) { p += 6; n -= 6; }
}

// PNG のチャンクを順に渡す。f(type4, body, len) が false を返したら止める。IEND を渡したあとで終わる。
// 署名が無ければ false。途中で切れたチャンク・型が英字でないチャンクの手前で終わる
template <class F> bool ForEachPngChunk(const uint8_t* d, size_t n, F f) {
    if (!HasPngSig(d, n)) return false;
    size_t p = 8;
    while (p + 12 <= n) {
        uint32_t len = Be32(d + p); const uint8_t* t = d + p + 4;
        bool alpha = true; for (int i = 0; i < 4; i++) if (!isalpha(t[i])) alpha = false;
        if (!alpha || p + 12 + (size_t)len > n) break;
        if (!f(t, d + p + 8, len)) break;
        if (!memcmp(t, "IEND", 4)) break;
        p += 12 + (size_t)len;
    }
    return true;
}

// JPEG のマーカーを順に渡す。f(marker, body, bodyLen) が false を返したら止める（body は長さ2バイトの後ろ）。
// SOS / EOI で終わる。JPEG でなければ false
template <class F> bool ForEachJpegSegment(const uint8_t* d, size_t n, F f) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t p = 2;
    while (p + 4 <= n && d[p] == 0xFF) {
        uint8_t m = d[p + 1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { p += 2; continue; }
        if (m == 0xDA || m == 0xD9) break;
        size_t len = (d[p + 2] << 8) | d[p + 3];
        if (len < 2 || p + 2 + len > n) break;
        if (!f(m, d + p + 4, len - 2)) break;
        p += 2 + len;
    }
    return true;
}

// WebP（RIFF）のチャンクを順に渡す。f(fourcc, body, len) が false を返したら止める。WebP でなければ false
template <class F> bool ForEachWebpChunk(const uint8_t* d, size_t n, F f) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4)) return false;
    size_t p = 12;
    while (p + 8 <= n) {
        size_t len = Le32(d + p + 4);
        if (p + 8 + len > n) break;
        if (!f(d + p, d + p + 8, len)) break;
        p += 8 + len + (len & 1);
    }
    return true;
}
