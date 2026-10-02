TLIV — Tenokun's Light Image Viewer
超軽量の Windows 用画像ビュワー / A very light image viewer for Windows

----------------------------------------
[English]
----------------------------------------
How to run
- Installer: run TLIVSetup-<version>.exe (per-user, no administrator rights needed).
  Uninstall from Windows Settings > Apps > Installed apps.
- Single exe: just run TLIV.exe. Settings are saved in TLIV.ini next to it.
- To open images with TLIV: right-click an image > Open with > Choose another app
  > TLIV. (Installer only. TLIV never takes over your default app.)

Keys and mouse
 Wheel / Ctrl+Wheel        Zoom at cursor
 Shift+Wheel / Left, Right Previous / next image
 Left drag                 Pan (in Select mode: select area)
 S                         Select mode on / off (off at startup)
 Space / Click             Play / pause animation (GIF, APNG, WebP)
 Q / E                     Previous / next frame
 Ctrl+A                    Select all
 Ctrl+C                    Copy selection (or whole image), keeps transparency
 Esc                       Deselect
 Delete                    Move to Recycle Bin (asks first)
 R                         Reset zoom and position
 B                         Background: checker / black / white / custom
 P                         Pixel mode / normal mode
 I                         Info panel (file, image and embedded text; read-only)
 Ctrl+O                    Open...
 Ctrl+,                    Settings...

Modes (P)
- Pixel mode: no smoothing, whole-number zoom steps, pixel-snapped. Shows x, y and HEX colour.
- Normal mode: continuous zoom, no pixel-snapping. Enlarging is never smoothed; only shrinking is smoothed (high quality).

Formats: PNG, JPEG, BMP, GIF (animated), WebP, APNG, ICO, SVG (simple).
Requires: Windows 10 / 11 (x64).

Note
- TLIV is not code-signed, so Windows SmartScreen may warn you.
  Click "More info" > "Run anyway".
- Use at your own risk.
- MIT License. Free to use.

----------------------------------------
[日本語]
----------------------------------------
動かし方
- インストーラー：TLIVSetup-<version>.exe を実行します（ユーザー単位。管理者権限は不要）。
  アンインストールは Windows の設定「アプリ > インストールされているアプリ」から。
- exe 単体：TLIV.exe をそのまま実行します。設定は隣の TLIV.ini に保存されます。
- 画像を TLIV で開く：画像を右クリック > プログラムから開く > 別のプログラムを選択
  > TLIV。（インストーラー版のみ。既定のアプリは奪いません。）

操作
 ホイール / Ctrl+ホイール   カーソル位置を中心に拡大・縮小
 Shift+ホイール / ← →      前後の画像へ
 左ドラッグ                 表示位置を動かす（パン）。範囲選択モード中は範囲選択
 S                          範囲選択モードの切り替え（起動時はオフ）
 Space / クリック            アニメの再生・停止（GIF、APNG、WebP）
 Q / E                      前のコマ / 次のコマ
 Ctrl+A                     画像全体を選択
 Ctrl+C                     選択範囲（なければ画像全体）をコピー。透過は保つ
 Esc                        選択を解除
 Delete                     ごみ箱へ送る（確認あり）
 R                          倍率と位置を戻す
 B                          透明部分の背景：チェック → 黒 → 白 → 任意色
 P                          モード切り替え
 I                          情報パネル（ファイル・画像・埋め込みテキストを表示。読み取り専用）
 Ctrl+O                     開く
 Ctrl+,                     設定

モード（P）
- ドット絵モード：補間なし、整数倍の段で拡大縮小、実ピクセルに合わせて描画。座標と HEX 色を表示。
- 通常モード：連続的な拡大縮小、実ピクセルへの位置合わせなし。拡大は補間なし、縮小のときだけ高品質に補間。

対応形式：PNG, JPEG, BMP, GIF（アニメ）, WebP, APNG, ICO, SVG（簡易）
動作環境：Windows 10 / 11（x64）

注意
- コード署名がないため、Windows の SmartScreen が警告を出すことがあります。
  「詳細情報」→「実行」を選んでください。
- 自己責任でお使いください。
- MIT ライセンスです。自由に使ってください。

----------------------------------------
[简体中文]
----------------------------------------
运行方法
- 安装程序：运行 TLIVSetup-<version>.exe（仅为当前用户安装，无需管理员权限）。
  卸载请在 Windows 设置 > 应用 > 已安装的应用 中进行。
- 单个 exe：直接运行 TLIV.exe。设置保存在旁边的 TLIV.ini 中。
- 用 TLIV 打开图像：右键单击图像 > 打开方式 > 选择其他应用
  > TLIV。（仅限安装程序版。TLIV 不会占用你的默认应用。）

操作
 滚轮 / Ctrl+滚轮            以光标位置为中心缩放
 Shift+滚轮 / ← →            上一张 / 下一张
 左键拖动                    平移（选区模式中为选取区域）
 S                           开启 / 关闭选区模式（启动时为关闭）
 空格键 / 单击               播放 / 暂停动画（GIF、APNG、WebP）
 Q / E                       上一帧 / 下一帧
 Ctrl+A                      全选
 Ctrl+C                      复制选区（没有选区则复制整张图像）。保留透明
 Esc                         取消选择
 Delete                      移到回收站（会先确认）
 R                           重置缩放和位置
 B                           背景：棋盘格 / 黑 / 白 / 自定义
 P                           像素模式 / 普通模式
 I                           信息面板（文件、图像和内嵌文本；只读）
 Ctrl+O                      打开...
 Ctrl+,                      设置...

模式（P）
- 像素模式：不做平滑，按整数倍缩放，对齐到实际像素。显示 x、y 和 HEX 颜色。
- 普通模式：连续缩放，不对齐像素。放大时从不平滑，只有缩小时才平滑（高质量）。

支持格式：PNG, JPEG, BMP, GIF（动画）, WebP, APNG, ICO, SVG（简易）
运行环境：Windows 10 / 11（x64）

注意
- TLIV 没有代码签名，Windows SmartScreen 可能会发出警告。
  请选择“更多信息”→“仍要运行”。
- 请自行承担使用风险。
- MIT 许可证。可自由使用。

----------------------------------------
[繁體中文]
----------------------------------------
執行方法
- 安裝程式：執行 TLIVSetup-<version>.exe（僅為目前使用者安裝，不需要系統管理員權限）。
  解除安裝請至 Windows 設定 > 應用程式 > 已安裝的應用程式。
- 單一 exe：直接執行 TLIV.exe。設定儲存在旁邊的 TLIV.ini。
- 用 TLIV 開啟影像：在影像上按右鍵 > 開啟檔案 > 選擇其他應用程式
  > TLIV。（僅限安裝程式版。TLIV 不會搶走你的預設應用程式。）

操作
 滾輪 / Ctrl+滾輪            以游標位置為中心縮放
 Shift+滾輪 / ← →            上一張 / 下一張
 左鍵拖曳                    平移（選取模式中為選取範圍）
 S                           開啟 / 關閉選取模式（啟動時為關閉）
 空白鍵 / 按一下             播放 / 暫停動畫（GIF、APNG、WebP）
 Q / E                       上一格 / 下一格
 Ctrl+A                      全選
 Ctrl+C                      複製選取範圍（沒有選取則複製整張影像）。保留透明
 Esc                         取消選取
 Delete                      移到資源回收筒（會先確認）
 R                           重設縮放與位置
 B                           背景：棋盤格 / 黑 / 白 / 自訂
 P                           像素模式 / 一般模式
 I                           資訊面板（檔案、影像和內嵌文字；唯讀）
 Ctrl+O                      開啟...
 Ctrl+,                      設定...

模式（P）
- 像素模式：不做平滑，按整數倍縮放，對齊到實際像素。顯示 x、y 和 HEX 顏色。
- 一般模式：連續縮放，不對齊像素。放大時從不平滑，只有縮小時才平滑（高品質）。

支援格式：PNG, JPEG, BMP, GIF（動畫）, WebP, APNG, ICO, SVG（簡易）
執行環境：Windows 10 / 11（x64）

注意
- TLIV 沒有程式碼簽章，Windows SmartScreen 可能會顯示警告。
  請選擇「其他資訊」→「仍要執行」。
- 使用風險請自行承擔。
- MIT 授權。可自由使用。
