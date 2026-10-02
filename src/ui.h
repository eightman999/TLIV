#pragma once
#include "common.h"

enum Cmd {
    ID_OPEN = 100, ID_SHOWEXP, ID_SETTINGS, ID_EXIT,
    ID_COPY, ID_SELALL, ID_DESEL, ID_DELETE,
    ID_PIXEL, ID_ZOOMIN, ID_ZOOMOUT, ID_RESET, ID_BGCYCLE,
    ID_KEYS, ID_ABOUT, ID_PREV, ID_NEXT, ID_INFO,
    ID_SELMODE, ID_PLAY, ID_FPREV, ID_FNEXT,
};

struct StatusInfo {
    std::wstring name, index, size, frame, zoom, mode, pos, color;
    bool basic = false;       // 名前・番号以外の欄を出すか
    bool pixel = false;       // ドット絵モード（座標・色の欄を足す）
    bool hasSwatch = false;
    COLORREF swatch = 0;
    bool operator==(const StatusInfo& o) const {
        return name == o.name && index == o.index && size == o.size && frame == o.frame && zoom == o.zoom && mode == o.mode && pos == o.pos &&
               color == o.color && basic == o.basic && pixel == o.pixel && hasSwatch == o.hasSwatch && swatch == o.swatch;
    }
};

struct UiState {
    int hoverTool = -1, pressTool = -1;
    int hoverMenu = -1, openMenu = -1;
    bool pixelOn = false, infoOn = false, selectOn = false, playOn = false, animated = false;
    StatusInfo status;
};

namespace ui {
struct Layout { RECT menu, toolbar, wrap, view, status, splitter, panel; };   // splitter / panel は情報パネルが出ているときだけ

void SetSize(int cw, int ch);
void Init();                       // アイコンとフォントの読み込み
void RebuildFont();                // g_scale に合わせて g_font を作り直す
HFONT CreateUiFont(int scale);     // UI と同じフォントを scale 倍で作る（呼んだ側が DeleteObject）
const wchar_t* FaceName();         // 今の言語で実際に使う書体名（RebuildFont で決まる）
int FacePx();                      // その書体の高さ（12。Tahoma のときだけ 11）
void SetPanel(bool on, int w);     // 情報パネルの表示と幅（論理px。幅は仕切りを含む）
Layout Calc(int cw, int ch);
void Paint(HDC hdc, int cw, int ch, const UiState& st);

RECT MenuItemRect(int i);          // クライアント座標
int HitMenu(int x, int y);
int ToolCount();                   // 区切りを除いたボタン数
RECT ToolRect(int i);
int HitTool(int x, int y);
int ToolCmd(int i);
bool ToolEnabled(int i, bool animated);   // play / fback / fnext は静止画で無効
int MinClientWidth();              // ツールバーのボタンが全部見える最小幅（論理px）
const wchar_t* ToolTip(int i);
HMENU BuildMenu(int idx);
RECT StatusRect();
}
