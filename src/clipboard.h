#pragma once
#include "common.h"

// 画像の一部（r の範囲）をクリップボードへコピーする。PNG（透過を保つ）と DIBV5 の2形式で置く。
// src はプリマルチ BGRA、srcW は src の横幅。
// Ok：どちらかの形式で置けた / NoMemory：PNG も DIB も作れなかった（メモリ不足。std::bad_alloc は呼び出し側が受ける）/ Busy：クリップボードを開けなかった・範囲が空
enum class CopyResult { Ok, NoMemory, Busy };
CopyResult CopyToClipboard(HWND owner, const uint32_t* src, int srcW, const RECT& r);
