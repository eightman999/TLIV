#pragma once
#include "common.h"

// 表示領域（Direct2D）。倍率・位置・選択・背景・モードを持つ
namespace viewer {
bool Create(HWND parent);
HWND Hwnd();
// 描画は垂直同期1回に1回。メッセージループが NeedsFrame のときだけ FrameHandle を待ち、RenderIfDirty を呼ぶ
bool NeedsFrame();
HANDLE FrameHandle();
void RenderIfDirty();
void LoopAlive();   // メインのループが回っている印（50ms 途絶えると InvalidateRect の保険が効く）
void Move(int x, int y, int w, int h);
// アニメ：コマ送りは QPC の絶対時刻。ループは AnimTimeout を待ちの上限にし、待ったあとで AnimTick を呼ぶ
DWORD AnimTimeout();                                       // 次のコマまでの残り ms。再生していなければ INFINITE
void AnimTick();                                           // 時刻が来ていればコマを進めて描き直しの印を付ける

bool SetImage(std::unique_ptr<Image> img, bool keepView);  // false: 描画準備に失敗（SVG など）
void SetFailed();                                          // "Cannot open this file" を出す
void Clear();
void OnScaleChanged();                                     // UI 倍率が変わった

const Image* Cur();
bool Failed();
const std::vector<uint32_t>& CurPixels();                  // 表示中フレーム（プリマルチ BGRA）

bool Pixel();
void SetPixel(bool on);
int Bg();                                                  // 0 checker, 1 black, 2 white, 3 custom
void SetBg(int b);
COLORREF Custom();
void SetCustom(COLORREF c);

float Zoom();
void ZoomStep(int dir);                                    // ビュー中心で1段
void Reset();

bool SelectMode();                                         // 範囲選択ボタン（起動時はオフ。保存しない）
void SetSelectMode(bool on);                               // オフにすると選択も消える

// アニメ（コマが2枚以上）の再生・停止とコマ送り。静止画では何もしない
bool Animated();
bool Playing();
void TogglePlay();
void StepFrame(int d);                                     // 止めてから1コマ。端では反対の端へ回る
int FrameIndex();
int FrameCount();

void SelectAll();
void Deselect();
RECT CropRect();                                           // 選択があれば選択範囲、なければ画像全体
bool SelDragging(int& w, int& h);

bool Probe(int& x, int& y, uint32_t& pbgra);               // マウス下のピクセル
}
