#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// 画像1枚分（全フレーム合成済み、プリマルチ BGRA）
struct Frame { std::vector<uint32_t> px; int delay = 100; };
struct Image {
    int w = 0, h = 0;
    std::vector<Frame> frames;
    bool svg = false;
    std::vector<char> svgData;
    int orient = 0;          // EXIF の向き（0=なし、1〜8）。画素は回転済み
    int framesTotal = 0;     // ファイル中のフレーム数（アニメのとき）
    bool memLimited = false; // メモリ上限で途中までしか展開していない
    bool frameLimited = false; // コマ数の上限（kMaxFrames）で途中までしか読んでいない
};

extern IWICImagingFactory* g_wic;
extern int g_scale;      // UI の整数倍率 floor(DPI/96)
extern HFONT g_font;     // UI フォント（現在の倍率）

// main.cpp が実装
void OnViewChanged();            // ビューの状態が変わった（ステータス再描画）
void NavigateRel(int d);         // 前後の画像へ
void OnFilesDropped(HDROP h);

inline COLORREF RGBc(unsigned v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
