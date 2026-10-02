#pragma once
#include "common.h"
#include <atomic>
#include <functional>

// 情報パネルの中身を作る。重い処理（ファイルの読み込み・解析・色数の集計）は別スレッドで行う
struct InfoReq {
    std::wstring path;
    bool decoded = false;        // TLIV が画像として読めたか
    bool svg = false;
    int w = 0, h = 0;
    int frames = 1;
    int orient = 0;              // EXIF の向き（0/1 は回転なし）。w, h は回転後
    int framesTotal = 0;         // ファイル中のフレーム数（上限で切ったときだけ意味がある）
    bool memLimited = false;     // メモリ上限で途中までしか展開していない
    bool frameLimited = false;   // コマ数の上限（kMaxFrames）で途中までしか読んでいない
    long long totalMs = 0;       // 表示側の遅延の合計（GIF の Frames 行に使う）
};

struct InfoResult {
    std::wstring text;           // 改行は CRLF（EDIT にそのまま渡せる）
    int colorsOff = -1, colorsLen = 0;   // "counting..." の位置（EDIT 上の文字位置）
    bool noWrap = false;         // 大きすぎて、折り返す EDIT では固まる（折り返さない EDIT で出す）
};

using InfoTextFn = std::function<void(InfoResult&&)>;
using InfoColorsFn = std::function<void(std::wstring&&)>;

// ワーカースレッドの本体。text を1回、色数が数え終わったら colors を1回呼ぶ。cancel が立ったらやめる
void RunInfoJob(const InfoReq& rq, std::shared_ptr<std::atomic<bool>> cancel, InfoTextFn onText, InfoColorsFn onColors);
