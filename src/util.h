#pragma once
#include "common.h"

// 展開するピクセルデータ（全フレーム合計）の上限
constexpr int kMemLimitGB = 1;
constexpr unsigned long long kMemLimit = (1ull << 30) * kMemLimitGB;

// 安全のための上限（名前付きの定数にまとめる）
constexpr int kMaxFrames = 10000;                        // GIF / APNG / アニメ WebP の最大コマ数。超えた分は読まない
constexpr size_t kInflateMax = 8u << 20;                 // 圧縮テキスト（zTXt / 圧縮 iTXt）1件の展開後の上限 8 MB
constexpr size_t kTextMax = 2000000;                     // 情報パネルに出すテキスト1件の上限（文字数）
constexpr size_t kPanelMax = 8000000;                    // 情報パネル全体の上限（文字数）
// 情報パネルの EDIT は、折り返しの計算が文字数に比例して（空白の無い長い列では2乗で）遅くなる。
// 次のどちらかを超える中身は、折り返さない表示（横スクロール）にして固まらないようにする。ふつうのファイルは超えない
constexpr size_t kWrapMaxChars = 300000;                 // 折り返して出す文字数の上限
constexpr size_t kWrapMaxRun = 20000;                    // 空白・改行をはさまない文字の連なりの上限

// ファイルを丸ごと読む。0 バイト・1GB 以上・読めないときは false
bool ReadFileAll(const std::wstring& path, std::vector<uint8_t>& buf);
// メモリ上のバイト列を読む IStream（SHCreateMemStream）。作れなければ null
ComPtr<IStream> MemStream(const void* data, size_t len);

// パスの最後の名前（区切りが無ければ全体）と、そのフォルダ（区切りが無ければ "."）
std::wstring NameOf(const std::wstring& path);
std::wstring DirOf(const std::wstring& path);

// プリマルチ BGRA → ストレート BGRA（a=0 は 0）
uint32_t Unpremultiply(uint32_t pbgra);
