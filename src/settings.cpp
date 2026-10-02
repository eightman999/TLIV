#include "settings.h"
#include <shlwapi.h>
#include <climits>
#include <cstdio>

namespace settings {

static const wchar_t* kSection = L"TLIV";
static const wchar_t* kBgNames[] = { L"checker", L"black", L"white", L"custom" };

// exe と同じ場所・同じ名前の .ini
static const std::wstring& IniPath() {
    static const std::wstring p = [] {
        wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
        PathRemoveExtensionW(b);
        return std::wstring(b) + L".ini";
    }();
    return p;
}
static void PutInt(const wchar_t* k, int v) { wchar_t b[32]; swprintf_s(b, L"%d", v); WritePrivateProfileStringW(kSection, k, b, IniPath().c_str()); }
static int GetInt(const wchar_t* k, int d) { return GetPrivateProfileIntW(kSection, k, d, IniPath().c_str()); }

void Load(Data& d) {
    const wchar_t* ini = IniPath().c_str();
    wchar_t buf[64];
    GetPrivateProfileStringW(kSection, L"CustomColor", L"5A7A8A", buf, 64, ini);
    d.custom = RGBc(wcstoul(buf, nullptr, 16));
    GetPrivateProfileStringW(kSection, L"Mode", L"pixel", buf, 64, ini);
    d.pixel = _wcsicmp(buf, L"normal") != 0;
    GetPrivateProfileStringW(kSection, L"Background", L"checker", buf, 64, ini);
    d.bg = 0;
    for (int i = 1; i < 4; i++) if (!_wcsicmp(buf, kBgNames[i])) d.bg = i;

    GetPrivateProfileStringW(kSection, L"Language", L"en", buf, 64, ini);
    d.lang = lang::FromIni(buf);

    d.infoOn = GetInt(L"InfoPanel", 0) != 0;
    d.infoW = GetInt(L"InfoWidth", kInfoWidthDefault);
    d.maximized = GetInt(L"Maximized", 0) != 0;

    int sx = GetInt(L"X", INT_MIN), sy = GetInt(L"Y", INT_MIN), sw = GetInt(L"Width", 0), sh = GetInt(L"Height", 0);
    if (sx != INT_MIN && sw >= 200 && sh >= 150) {
        RECT r = { sx, sy, sx + sw, sy + sh };
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { d.x = sx; d.y = sy; d.w = sw; d.h = sh; }
    }
}

void Save(const Data& d) {
    const wchar_t* ini = IniPath().c_str();
    WritePrivateProfileStringW(kSection, L"Mode", d.pixel ? L"pixel" : L"normal", ini);
    WritePrivateProfileStringW(kSection, L"Background", kBgNames[d.bg], ini);
    wchar_t c[16]; swprintf_s(c, L"%02X%02X%02X", GetRValue(d.custom), GetGValue(d.custom), GetBValue(d.custom));
    WritePrivateProfileStringW(kSection, L"CustomColor", c, ini);
    PutInt(L"X", d.x); PutInt(L"Y", d.y);
    PutInt(L"Width", d.w); PutInt(L"Height", d.h);
    PutInt(L"Maximized", d.maximized ? 1 : 0);
    PutInt(L"InfoPanel", d.infoOn ? 1 : 0);
    PutInt(L"InfoWidth", d.infoW);
    SaveLanguage(d.lang);
}

void SaveLanguage(lang::Lang l) { WritePrivateProfileStringW(kSection, L"Language", lang::IniCode(l), IniPath().c_str()); }

}  // namespace settings
