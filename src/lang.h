#pragma once
#include "common.h"

// 表示言語。文字列は lang_strings.h の表1か所にある
namespace lang {

enum Lang { EN = 0, JA, ZH_CN, ZH_TW, kCount };

enum Id {
#define X(id, en, ja, zhcn, zhtw) id,
#include "lang_strings.h"
    TLIV_STRINGS(X)
#undef X
    kIdCount
};

void Set(Lang l);
Lang Get();
const wchar_t* T(Id id);                          // 今の言語の文字列
std::wstring Sub(Id id, const std::wstring& s);   // 最初の %s を s に置き換える

const wchar_t* IniCode(Lang l);                   // ini の Language= の値（en / ja / zh-CN / zh-TW）
Lang FromIni(const wchar_t* code);                // 知らない値は English
const wchar_t* NativeName(Lang l);                // 選択肢の表記（どの言語でも同じ）
LANGID MsgLangId(Lang l);                         // FormatMessage 用

// UI の書体の候補（先頭から順に、入っているものを使う。最後は必ず使う）。px は 12 か 11
struct FaceCand { const wchar_t* face; int px; };
const FaceCand* FaceCands(Lang l, int& n);

}  // namespace lang
