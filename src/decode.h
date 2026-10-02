#pragma once
#include "common.h"
// WIC / APNG / GIF で読む。SVG は生データだけ持たせる（描画は viewer 側）
bool DecodeFile(const std::wstring& path, Image& out);
bool DecodePngResource(const void* data, size_t len, int& w, int& h, std::vector<uint32_t>& px);
