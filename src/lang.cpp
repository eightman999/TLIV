#include "lang.h"
#include <iterator>

namespace lang {

static const wchar_t* const kStrings[kIdCount][kCount] = {
#define X(id, en, ja, zhcn, zhtw) { en, ja, zhcn, zhtw },
    TLIV_STRINGS(X)
#undef X
};

// 言語ごとの定義。言語を足すときは Lang と、ここと、lang_strings.h の列を足す
struct LangInfo { const wchar_t* ini; const wchar_t* name; LANGID msgLang; };
static const LangInfo kLangs[kCount] = {
    { L"en",    L"English",  MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) },
    { L"ja",    L"日本語",   MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT) },
    { L"zh-CN", L"简体中文", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED) },
    { L"zh-TW", L"繁體中文", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL) },
};

// 書体：すべて 12px（Tahoma だけ 11px）。NONANTIALIASED で埋め込みビットマップのドット文字を出す
static const FaceCand kFaceEnJa[] = { { L"MS UI Gothic", 12 }, { L"Tahoma", 11 } };
static const FaceCand kFaceZhCn[] = { { L"SimSun", 12 }, { L"MS UI Gothic", 12 } };
static const FaceCand kFaceZhTw[] = { { L"PMingLiU", 12 }, { L"MingLiU", 12 }, { L"SimSun", 12 }, { L"MS UI Gothic", 12 } };

static Lang g_lang = EN;

void Set(Lang l) { g_lang = (l >= 0 && l < kCount) ? l : EN; }
Lang Get() { return g_lang; }
const wchar_t* T(Id id) { return kStrings[id][g_lang]; }

std::wstring Sub(Id id, const std::wstring& s) {
    std::wstring f = T(id);
    size_t p = f.find(L"%s");
    if (p != std::wstring::npos) f.replace(p, 2, s);
    return f;
}

const wchar_t* IniCode(Lang l) { return kLangs[l].ini; }
Lang FromIni(const wchar_t* code) {
    for (int i = 0; i < kCount; i++) if (!_wcsicmp(code, kLangs[i].ini)) return (Lang)i;
    return EN;
}
const wchar_t* NativeName(Lang l) { return kLangs[l].name; }
LANGID MsgLangId(Lang l) { return kLangs[l].msgLang; }

const FaceCand* FaceCands(Lang l, int& n) {
    switch (l) {
    case ZH_CN: n = (int)std::size(kFaceZhCn); return kFaceZhCn;
    case ZH_TW: n = (int)std::size(kFaceZhTw); return kFaceZhTw;
    default: n = (int)std::size(kFaceEnJa); return kFaceEnJa;
    }
}

}  // namespace lang
