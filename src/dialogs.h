#pragma once
#include "common.h"

// 確認・通知ダイアログ（32px のアイコン付き）。確認の初期フォーカスは常に No
enum class DlgIcon { Recycler, Warning, Error };
struct DlgThumb {                  // 確認に添える画像（表示中のフレーム、プリマルチ BGRA）
    const uint32_t* px = nullptr; int w = 0, h = 0;
    bool pixel = false;            // ドット絵モード（補間なし）か
};
// Yes のときだけ true。l1 が主文（bold1 なら太字）、l2・l3 は補足（空なら出さない）。thumb が null ならサムネイルなし
bool ConfirmDialog(HWND owner, DlgIcon icon, const std::wstring& l1, bool bold1, const std::wstring& l2, const std::wstring& l3, const DlgThumb* thumb);
// エラーアイコンの通知。ボタンは OK だけ
void NoticeDialog(HWND owner, const std::wstring& l1, const std::wstring& l2);

struct SettingsData {
    bool pixel = true;
    int bg = 0;                 // 0 checker, 1 black, 2 white, 3 custom
    COLORREF custom = 0;
    bool noConfirm = false;     // ini には保存しない
    int lang = 0;               // lang::Lang
};
// OK なら true（s に結果）。Cancel なら s は変えない
bool SettingsDialog(HWND owner, SettingsData& s);
