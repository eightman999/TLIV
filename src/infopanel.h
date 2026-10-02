#pragma once
#include "common.h"
#include "meta.h"

// 右側の情報パネル（読み取り専用のマルチライン EDIT）
namespace infopanel {
const UINT WM_INFO_TEXT = WM_APP + 2;     // lParam: new InfoMsg*
const UINT WM_INFO_COLORS = WM_APP + 3;   // lParam: new InfoMsg*
struct InfoMsg { unsigned serial = 0; InfoResult res; std::wstring colors; };

bool Create(HWND parent, int id);
HWND Hwnd();
void SetFont(int scale);                      // UI 倍率が変わったとき
void Place(int x, int y, int w, int h);       // 実ピクセル
void Show(bool on);
void Request(const InfoReq& rq);              // 中身を作り直す（非同期。裏の作業は常に1本で、新しい注文は待っている注文を上書きする）
void Clear();
void Shutdown();                              // 終了時：作業は待たない（止めの合図だけ送る）
void OnText(InfoMsg* m);                      // WM_INFO_TEXT を受けた
void OnColors(InfoMsg* m);                    // WM_INFO_COLORS を受けた
}
