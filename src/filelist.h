#pragma once
#include "common.h"

struct FileList {
    std::wstring dir;
    std::vector<std::wstring> paths;   // フルパス
    int cur = -1;
    bool fromShell = false;            // Explorer の表示順を使っているか
};

bool IsSupportedFile(const wchar_t* path);
// 「開く」ダイアログのフィルタ（OPENFILENAME::lpstrFilter 用。対応拡張子の一覧から作る）
std::wstring OpenFileFilter();   // 今の言語のフィルタ名で作る
// path のフォルダの一覧を作る（Explorer の表示順が取れれば使う）
void BuildList(const std::wstring& path, FileList& fl);
// フォルダの中身が変わった後の更新。現在のファイルは名前で追う
void RefreshList(FileList& fl);

// ReadDirectoryChangesW でフォルダを監視し、変化があれば hwnd に msg を投げる
void StartWatch(HWND hwnd, UINT msg, const std::wstring& dir);
void StopWatch();
