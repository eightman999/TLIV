#pragma once
#include "common.h"

enum class DeleteResult {
    Kept,          // 消さなかった（やめた・失敗した。失敗のときは通知ダイアログを出し済み）
    Deleted,       // ごみ箱へ送った（確認で完全削除と承知したときは完全に削除した）
    AlreadyGone,   // もう無かった
};

// 現在のファイルを消す一連の流れ：存在と属性の確認 → 確認ダイアログ → ごみ箱へ → 失敗の通知。
// loadedPath は表示中のファイル（確認ダイアログのサムネイルに使う）。noConfirm でも、完全に消えるときと
// 読み取り専用のときは必ず確認する。一覧からの除去と画面の更新は呼び出し側で行う
DeleteResult DeleteWithConfirm(HWND owner, const std::wstring& path, const std::wstring& loadedPath, bool noConfirm);
