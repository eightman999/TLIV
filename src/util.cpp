#include "util.h"
#include <shlwapi.h>
#include <algorithm>

bool ReadFileAll(const std::wstring& path, std::vector<uint8_t>& buf) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li; bool ok = GetFileSizeEx(f, &li) && li.QuadPart > 0 && li.QuadPart < (1LL << 30);
    if (ok) {
        try { buf.resize((size_t)li.QuadPart); } catch (...) { ok = false; }
        DWORD got = 0;
        if (ok) ok = ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got == buf.size();
    }
    CloseHandle(f);
    return ok;
}

ComPtr<IStream> MemStream(const void* data, size_t len) {
    ComPtr<IStream> s;
    s.Attach(SHCreateMemStream((const BYTE*)data, (UINT)len));
    return s;
}

std::wstring NameOf(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
std::wstring DirOf(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"." : p.substr(0, i); }

uint32_t Unpremultiply(uint32_t v) {
    uint32_t a = v >> 24;
    if (a == 0) return 0;
    if (a == 255) return v;
    uint32_t r = std::min(255u, (((v >> 16) & 255) * 255 + a / 2) / a), g = std::min(255u, (((v >> 8) & 255) * 255 + a / 2) / a), b = std::min(255u, ((v & 255) * 255 + a / 2) / a);
    return (a << 24) | (r << 16) | (g << 8) | b;
}
