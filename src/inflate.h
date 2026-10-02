#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// zlib 形式（2バイトのヘッダー＋deflate）を展開する。外部ライブラリは使わない。
// 壊れている・途中で切れている・maxOut を超えるときは false。maxOut を超えたためなら *tooLarge を true にする
bool ZlibInflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t maxOut = 256u << 20, bool* tooLarge = nullptr);
