#pragma once
#include "common.h"
#include "lang.h"

// 設定（exe の隣の TLIV.ini）の読み書き
namespace settings {

const int kInfoWidthDefault = 240;     // 情報パネルの既定の幅（論理px、仕切りを含む）

struct Data {
    bool pixel = true;                 // Mode: pixel / normal
    int bg = 0;                        // Background: 0 checker, 1 black, 2 white, 3 custom
    COLORREF custom = RGBc(0x5A7A8A);  // CustomColor
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 672, h = 520;   // 窓の位置と大きさ（通常時）
    bool maximized = false;
    bool infoOn = false;               // 情報パネルを出しているか
    int infoW = kInfoWidthDefault;
    lang::Lang lang = lang::EN;        // Language: en / ja / zh-CN / zh-TW
};

// 読む。無い項目は既定値。窓の位置は、どのモニターにも掛からないときは既定に戻す
void Load(Data& d);
void Save(const Data& d);
void SaveLanguage(lang::Lang l);       // Settings の OK ですぐ書く（次の起動でも同じ言語にする）

}  // namespace settings
