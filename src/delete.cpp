#include "delete.h"
#include "dialogs.h"
#include "viewer.h"
#include "util.h"
#include "lang.h"
#include <shobjidl.h>
#include <shlobj.h>

// ごみ箱へ送れる場所か。ネットワーク・USB メモリなど（ごみ箱が無い）、ごみ箱が無効の設定、ごみ箱の容量より大きいファイルは false
static bool CanRecycle(const std::wstring& path, ULONGLONG size) {
    wchar_t root[MAX_PATH];
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH)) return true;
    UINT t = GetDriveTypeW(root);
    if (t == DRIVE_REMOTE || t == DRIVE_REMOVABLE || t == DRIVE_CDROM || t == DRIVE_RAMDISK) return false;
    auto rd = [](const wchar_t* sub, const wchar_t* name, DWORD& v) {
        HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
        DWORD sz = 4, ty = 0; bool ok = RegQueryValueExW(k, name, nullptr, &ty, (BYTE*)&v, &sz) == ERROR_SUCCESS && ty == REG_DWORD;
        RegCloseKey(k); return ok;
    };
    static const wchar_t* base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket";
    DWORD nuke = 0, cap = 0; bool haveNuke = false, haveCap = false;
    wchar_t vol[64];
    if (GetVolumeNameForVolumeMountPointW(root, vol, 64)) {
        wchar_t* a = wcschr(vol, L'{'); wchar_t* b = wcschr(vol, L'}');
        if (a && b) {
            std::wstring sub = std::wstring(base) + L"\\Volume\\" + std::wstring(a, b + 1);
            haveNuke = rd(sub.c_str(), L"NukeOnDelete", nuke); haveCap = rd(sub.c_str(), L"MaxCapacity", cap);
        }
    }
    if (!haveNuke) rd(base, L"NukeOnDelete", nuke);
    if (nuke) return false;
    if (haveCap && cap && size > (ULONGLONG)cap * 1048576ull) return false;
    return true;
}

// 削除1件の結果を受け取る（失敗の HRESULT が欲しい）
struct DelSink : IFileOperationProgressSink {
    LONG ref = 1; HRESULT hr = S_OK;
    STDMETHODIMP QueryInterface(REFIID r, void** p) override {
        if (r == IID_IUnknown || r == IID_IFileOperationProgressSink) { *p = this; AddRef(); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&ref); if (!n) delete this; return n; }
    STDMETHODIMP StartOperations() override { return S_OK; }
    STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT h, IShellItem*) override { if (FAILED(h)) hr = h; return S_OK; }
    STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    STDMETHODIMP ResetTimer() override { return S_OK; }
    STDMETHODIMP PauseTimer() override { return S_OK; }
    STDMETHODIMP ResumeTimer() override { return S_OK; }
};

enum class DelResult { Ok, Aborted, Missing, Sharing, Denied, Other };

// ごみ箱へ送る。permanent は「ごみ箱が使えないと分かっていて、確認済み」のとき。
// それ以外では FOF_WANTNUKEWARNING を付けて、事前に判定できなかった完全削除を Windows の警告に任せる
static DelResult RecycleFile(HWND owner, const std::wstring& path, bool permanent, DWORD& code) {
    code = 0;
    ComPtr<IFileOperation> op; ComPtr<IShellItem> item; ComPtr<DelSink> sink; sink.Attach(new DelSink());
    DelResult r = DelResult::Other;
    HRESULT hr = E_FAIL;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) {
        op->SetOwnerWindow(owner);
        DWORD fl = FOF_NOCONFIRMATION | FOF_ALLOWUNDO | FOF_NOERRORUI | FOFX_RECYCLEONDELETE;
        if (!permanent) fl |= FOF_WANTNUKEWARNING;
        op->SetOperationFlags(fl);
        hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (SUCCEEDED(hr)) hr = op->DeleteItem(item.Get(), sink.Get());
        if (SUCCEEDED(hr)) {
            hr = op->PerformOperations();
            BOOL ab = FALSE; op->GetAnyOperationsAborted(&ab);
            if (SUCCEEDED(hr) && FAILED(sink->hr)) hr = sink->hr;
            if (SUCCEEDED(hr) && ab) { r = DelResult::Aborted; hr = S_OK; }
            else if (SUCCEEDED(hr)) r = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? DelResult::Ok : DelResult::Other;
        }
    }
    if (FAILED(hr)) {
        // 0x8007xxxx は Win32 のエラー番号。0x8027xxxx はシェルのコピーエンジンの独自コード（COPYENGINE_E_*）
        switch (hr) {
        case E_ACCESSDENIED: case (HRESULT)0x80270021: case (HRESULT)0x80270022: code = ERROR_ACCESS_DENIED; break;          // ACCESS_DENIED_SRC / DEST
        case (HRESULT)0x80270027: case (HRESULT)0x80270028: code = ERROR_SHARING_VIOLATION; break;                        // SHARING_VIOLATION_SRC / DEST
        case (HRESULT)0x8027001D: case (HRESULT)0x8027001E: code = ERROR_FILENAME_EXCED_RANGE; break;                     // PATH_TOO_DEEP
        default: code = (hr & 0xFFFF0000) == 0x80070000 ? (hr & 0xFFFF) : 0;
        }
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) r = DelResult::Sharing;
        else if (code == ERROR_ACCESS_DENIED) r = DelResult::Denied;
        else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) r = DelResult::Missing;
        else r = DelResult::Other;
        if (!code) code = (DWORD)hr;
    }
    return r;
}

// FormatMessage の文は、選んだ言語 → 英語 → 既定の順に試す（その言語の文が OS に無いことがある）
static std::wstring ErrorText(DWORD code) {
    wchar_t b[256] = L"";
    const DWORD fl = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD n = FormatMessageW(fl, nullptr, code, lang::MsgLangId(lang::Get()), b, 256, nullptr);
    if (!n) n = FormatMessageW(fl, nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), b, 256, nullptr);
    if (!n) FormatMessageW(fl, nullptr, code, 0, b, 256, nullptr);
    std::wstring s = b;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    if (s.empty()) { wchar_t t[64]; swprintf_s(t, lang::T(lang::UNKNOWN_ERR), (unsigned)code); s = t; }
    return s;
}

DeleteResult DeleteWithConfirm(HWND owner, const std::wstring& path, const std::wstring& loadedPath, bool noConfirm) {
    std::wstring name = NameOf(path);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return DeleteResult::AlreadyGone;   // もう消えている
        NoticeDialog(owner, lang::Sub(lang::CANT_DELETE, name), ErrorText(e));
        return DeleteResult::Kept;
    }
    bool ro = (fa.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    bool bin = CanRecycle(path, ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow);
    // 消す画像のサムネイル（表示中のフレーム）
    DlgThumb th; const DlgThumb* pth = nullptr;
    const Image* im = viewer::Cur();
    if (im && !im->svg && !viewer::Failed() && !viewer::CurPixels().empty() && _wcsicmp(path.c_str(), loadedPath.c_str()) == 0) {
        th.px = viewer::CurPixels().data(); th.w = im->w; th.h = im->h; th.pixel = viewer::Pixel(); pth = &th;
    }
    bool go;
    if (!bin)   // 完全に消える。確認なしの設定に関係なく必ず確認
        go = ConfirmDialog(owner, DlgIcon::Warning, lang::Sub(lang::PERMANENT, name), true, lang::T(lang::NO_RESTORE), ro ? lang::T(lang::RO_NOTE) : L"", pth);
    else if (ro)   // 読み取り専用。確認なしの設定に関係なく必ず確認
        go = ConfirmDialog(owner, DlgIcon::Warning, lang::Sub(lang::RO_MAIN, name), true, lang::T(lang::RO_ASK), L"", pth);
    else if (!noConfirm)
        go = ConfirmDialog(owner, DlgIcon::Recycler, lang::Sub(lang::CONFIRM_BIN, name), false, L"", L"", pth);
    else go = true;
    if (!go) return DeleteResult::Kept;

    DWORD code = 0;
    switch (RecycleFile(owner, path, !bin, code)) {
    case DelResult::Ok: return DeleteResult::Deleted;
    case DelResult::Aborted: break;
    case DelResult::Missing: return DeleteResult::AlreadyGone;
    case DelResult::Sharing: NoticeDialog(owner, lang::Sub(lang::CANT_DELETE, name), lang::T(lang::IN_USE)); break;
    case DelResult::Denied: NoticeDialog(owner, lang::Sub(lang::CANT_DELETE, name), lang::T(lang::DENIED)); break;
    default: NoticeDialog(owner, lang::Sub(lang::CANT_DELETE, name), ErrorText(code)); break;
    }
    return DeleteResult::Kept;
}
