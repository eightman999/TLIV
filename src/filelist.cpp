#include "filelist.h"
#include "util.h"
#include "lang.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <algorithm>
#include <thread>

static const wchar_t* kExts[] = { L".png", L".apng", L".jpg", L".jpeg", L".jpe", L".jfif", L".bmp", L".dib", L".gif", L".webp", L".ico", L".svg" };

bool IsSupportedFile(const wchar_t* path) {
    const wchar_t* e = PathFindExtensionW(path);
    for (auto x : kExts) if (!_wcsicmp(e, x)) return true;
    return false;
}

// 開くダイアログのフィルタ（"Images\0*.png;...\0All files\0*.*\0"）。対応拡張子 kExts から作る
std::wstring OpenFileFilter() {
    std::wstring pat;
    for (auto x : kExts) { if (!pat.empty()) pat += L';'; pat += L'*'; pat += x; }
    std::wstring r = lang::T(lang::FILTER_IMAGES); r += L'\0'; r += pat; r += L'\0';
    r += lang::T(lang::FILTER_ALL); r += L'\0'; r += L"*.*"; r += L'\0';
    return r;   // c_str() の終端と合わせて、末尾は NUL が二重になる
}

static bool ScanDir(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (IsSupportedFile(fd.cFileName)) out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

static void NaturalSort(std::vector<std::wstring>& v) {
    std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(a.c_str() + a.find_last_of(L'\\') + 1, b.c_str() + b.find_last_of(L'\\') + 1) < 0;
    });
}

static bool SamePath(const std::wstring& a, const std::wstring& b) { return !_wcsicmp(a.c_str(), b.c_str()); }

static std::wstring TrimSlash(std::wstring s) { while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; }

// Explorer のウィンドウで dir を開いているものの表示順を取る（IShellWindows → IFolderView）
static bool ShellOrder(const std::wstring& dir, std::vector<std::wstring>& out) {
    ComPtr<IShellWindows> sw;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&sw))) || !sw) return false;
    long n = 0; sw->get_Count(&n);
    HWND fg = GetForegroundWindow();
    bool found = false;
    std::vector<std::wstring> best;
    for (long i = 0; i < n; i++) {
        VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = i;
        ComPtr<IDispatch> disp;
        if (FAILED(sw->Item(v, &disp)) || !disp) continue;
        ComPtr<IServiceProvider> sp; ComPtr<IShellBrowser> sb; ComPtr<IShellView> sv; ComPtr<IFolderView> fv;
        ComPtr<IPersistFolder2> pf; ComPtr<IWebBrowserApp> wb;
        PIDLIST_ABSOLUTE folder = nullptr;
        bool match = false;
        std::vector<std::wstring> items;
        if (SUCCEEDED(disp.As(&sp)) &&
            SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb))) &&
            SUCCEEDED(sb->QueryActiveShellView(&sv)) &&
            SUCCEEDED(sv.As(&fv)) &&
            SUCCEEDED(fv->GetFolder(IID_PPV_ARGS(&pf))) &&
            SUCCEEDED(pf->GetCurFolder(&folder))) {
            wchar_t buf[MAX_PATH];
            if (SHGetPathFromIDListW(folder, buf) && SamePath(TrimSlash(buf), TrimSlash(dir))) {
                int cnt = 0;
                if (SUCCEEDED(fv->ItemCount(SVGIO_ALLVIEW, &cnt))) {
                    for (int k = 0; k < cnt; k++) {
                        PITEMID_CHILD child = nullptr;
                        if (FAILED(fv->Item(k, &child)) || !child) continue;
                        PIDLIST_ABSOLUTE full = ILCombine(folder, child);
                        if (full) { if (SHGetPathFromIDListW(full, buf)) items.push_back(buf); ILFree(full); }
                        ILFree(child);
                    }
                    match = true;
                }
            }
        }
        if (match) {
            HWND hw = nullptr;
            if (SUCCEEDED(disp.As(&wb))) { SHANDLE_PTR h = 0; if (SUCCEEDED(wb->get_HWND(&h))) hw = (HWND)h; }
            if (!found || hw == fg) { best = items; found = true; }
        }
        if (folder) ILFree(folder);
    }
    if (!found) return false;
    for (auto& p : best) if (IsSupportedFile(p.c_str())) out.push_back(p);
    return true;
}

void BuildList(const std::wstring& path, FileList& fl) {
    fl = FileList();
    fl.dir = DirOf(path);
    std::vector<std::wstring> v;
    if (ShellOrder(fl.dir, v)) {
        for (size_t i = 0; i < v.size(); i++) if (SamePath(v[i], path)) { fl.paths = v; fl.cur = (int)i; fl.fromShell = true; return; }
    }
    ScanDir(fl.dir, fl.paths);
    NaturalSort(fl.paths);
    for (size_t i = 0; i < fl.paths.size(); i++) if (SamePath(fl.paths[i], path)) fl.cur = (int)i;
    if (fl.cur < 0) { fl.paths.push_back(path); fl.cur = (int)fl.paths.size() - 1; }
}

void RefreshList(FileList& fl) {
    std::wstring curp = (fl.cur >= 0 && fl.cur < (int)fl.paths.size()) ? fl.paths[fl.cur] : L"";
    std::vector<std::wstring> now;
    if (!ScanDir(fl.dir, now)) return;
    std::vector<std::wstring> out;
    bool shell = false;
    if (fl.fromShell) {
        std::vector<std::wstring> v;
        if (ShellOrder(fl.dir, v) && !v.empty()) { out = v; shell = true; }
    }
    if (!shell) {
        if (fl.fromShell) {
            // Explorer が閉じた等：既存の並びを保ち、消えたものを除き、増えたものを自然順で末尾へ
            for (auto& p : fl.paths) for (auto& q : now) if (SamePath(p, q)) { out.push_back(p); break; }
            std::vector<std::wstring> add;
            for (auto& q : now) { bool has = false; for (auto& p : out) if (SamePath(p, q)) { has = true; break; } if (!has) add.push_back(q); }
            NaturalSort(add);
            out.insert(out.end(), add.begin(), add.end());
        } else { out = now; NaturalSort(out); }
    }
    fl.paths = out;
    fl.fromShell = shell || fl.fromShell;
    int idx = -1;
    for (size_t i = 0; i < out.size(); i++) if (SamePath(out[i], curp)) idx = (int)i;
    if (idx < 0 && !curp.empty()) {
        // 現在のファイルが消えた：元の位置付近に留める
        idx = std::min(std::max(fl.cur, 0), (int)out.size() - 1);
    }
    fl.cur = idx;
}

// ---------------- 監視
static std::thread g_thread;
static HANDLE g_stop = nullptr;

void StopWatch() {
    if (g_stop) SetEvent(g_stop);
    if (g_thread.joinable()) g_thread.join();
    if (g_stop) { CloseHandle(g_stop); g_stop = nullptr; }
}

void StartWatch(HWND hwnd, UINT msg, const std::wstring& dir) {
    StopWatch();
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stop = g_stop;
    g_thread = std::thread([=]() {
        HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        alignas(DWORD) BYTE buf[16384];
        for (;;) {
            OVERLAPPED ov = {}; ov.hEvent = ev; ResetEvent(ev);
            if (!ReadDirectoryChangesW(h, buf, sizeof buf, FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION,
                nullptr, &ov, nullptr)) break;
            HANDLE w[2] = { ev, stop };
            DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);
            if (r != WAIT_OBJECT_0) { CancelIoEx(h, &ov); DWORD t; GetOverlappedResult(h, &ov, &t, TRUE); break; }
            DWORD got = 0;
            if (!GetOverlappedResult(h, &ov, &got, FALSE)) break;
            PostMessageW(hwnd, msg, 0, 0);
        }
        CloseHandle(ev); CloseHandle(h);
    });
}
