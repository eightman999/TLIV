// 画面の文字列の表（これが唯一の置き場）。X(ID, English, 日本語, 简体中文, 繁體中文)
// 言語を足すときは、各行に列を足し、lang.h の Lang と lang.cpp の kLangs を1つ増やす。
// %s は文字列（lang::Sub で差し込む）、%d は数。ポップアップメニューの & と \t 以降のキーは呼び出し側で足す
#define TLIV_STRINGS(X) \
    /* メニューバー（括弧つきの言語は、括弧の中の英字に下線） */ \
    X(M_FILE, L"File", L"ファイル(F)", L"文件(F)", L"檔案(F)") \
    X(M_EDIT, L"Edit", L"編集(E)", L"编辑(E)", L"編輯(E)") \
    X(M_VIEW, L"View", L"表示(V)", L"查看(V)", L"檢視(V)") \
    X(M_HELP, L"Help", L"ヘルプ(H)", L"帮助(H)", L"說明(H)") \
    /* ポップアップメニュー */ \
    X(P_OPEN, L"&Open...", L"開く(&O)...", L"打开(&O)...", L"開啟(&O)...") \
    X(P_SHOWEXP, L"Show in &Explorer", L"エクスプローラーで表示(&E)", L"在资源管理器中显示(&E)", L"在檔案總管中顯示(&E)") \
    X(P_SETTINGS, L"&Settings...", L"設定(&S)...", L"设置(&S)...", L"設定(&S)...") \
    X(P_EXIT, L"E&xit", L"終了(&X)", L"退出(&X)", L"結束(&X)") \
    X(P_COPY, L"&Copy", L"コピー(&C)", L"复制(&C)", L"複製(&C)") \
    X(P_SELMODE, L"Select &Mode", L"範囲選択モード(&M)", L"选区模式(&M)", L"選取模式(&M)") \
    X(P_SELALL, L"Select &All", L"すべて選択(&A)", L"全选(&A)", L"全選(&A)") \
    X(P_DESEL, L"&Deselect", L"選択を解除(&D)", L"取消选择(&D)", L"取消選取(&D)") \
    X(P_DELETE, L"De&lete", L"削除(&L)", L"删除(&L)", L"刪除(&L)") \
    X(P_ZOOMIN, L"Zoom &In", L"拡大(&I)", L"放大(&I)", L"放大(&I)") \
    X(P_ZOOMOUT, L"Zoom &Out", L"縮小(&O)", L"缩小(&O)", L"縮小(&O)") \
    X(P_RESET, L"&Reset Zoom", L"倍率を戻す(&R)", L"重置缩放(&R)", L"重設縮放(&R)") \
    X(P_INFO, L"Info &Panel", L"情報パネル(&P)", L"信息面板(&P)", L"資訊面板(&P)") \
    X(P_PLAY, L"Play / &Pause", L"再生 / 停止(&P)", L"播放 / 暂停(&P)", L"播放 / 暫停(&P)") \
    X(P_FPREV, L"Pre&vious Frame", L"前のコマ(&V)", L"上一帧(&V)", L"上一格(&V)") \
    X(P_FNEXT, L"&Next Frame", L"次のコマ(&N)", L"下一帧(&N)", L"下一格(&N)") \
    X(P_KEYS, L"&Keys...", L"キー操作(&K)...", L"快捷键(&K)...", L"快速鍵(&K)...") \
    X(P_ABOUT, L"&About", L"バージョン情報(&A)", L"关于(&A)", L"關於(&A)") \
    /* ツールチップ */ \
    X(TIP_PREV, L"Previous (Left / Shift+Wheel)", L"前の画像（← / Shift+ホイール）", L"上一张（← / Shift+滚轮）", L"上一張（← / Shift+滾輪）") \
    X(TIP_NEXT, L"Next (Right / Shift+Wheel)", L"次の画像（→ / Shift+ホイール）", L"下一张（→ / Shift+滚轮）", L"下一張（→ / Shift+滾輪）") \
    X(TIP_FPREV, L"Previous frame (Q)", L"前のコマ（Q）", L"上一帧（Q）", L"上一格（Q）") \
    X(TIP_PLAY, L"Play / Pause (Space, Click)", L"再生 / 停止（Space・クリック）", L"播放 / 暂停（空格键、单击）", L"播放 / 暫停（空白鍵、按一下）") \
    X(TIP_FNEXT, L"Next frame (E)", L"次のコマ（E）", L"下一帧（E）", L"下一格（E）") \
    X(TIP_ZOOMIN, L"Zoom in (Wheel)", L"拡大（ホイール）", L"放大（滚轮）", L"放大（滾輪）") \
    X(TIP_ZOOMOUT, L"Zoom out (Wheel)", L"縮小（ホイール）", L"缩小（滚轮）", L"縮小（滾輪）") \
    X(TIP_RESET, L"Reset zoom (R)", L"倍率を戻す（R）", L"重置缩放（R）", L"重設縮放（R）") \
    X(TIP_PIXEL, L"Pixel mode (P)", L"ドット絵モード（P）", L"像素模式（P）", L"像素模式（P）") \
    X(TIP_BG, L"Background (B)", L"背景（B）", L"背景（B）", L"背景（B）") \
    X(TIP_SELECT, L"Select (S)", L"範囲選択（S）", L"选区（S）", L"選取（S）") \
    X(TIP_COPY, L"Copy (Ctrl+C)", L"コピー（Ctrl+C）", L"复制（Ctrl+C）", L"複製（Ctrl+C）") \
    X(TIP_INFO, L"Info panel (I)", L"情報パネル（I）", L"信息面板（I）", L"資訊面板（I）") \
    X(TIP_DELETE, L"Delete (Del)", L"削除（Del）", L"删除（Del）", L"刪除（Del）") \
    /* ステータスバー・表示領域・開くダイアログ */ \
    X(ST_PIXEL, L"Pixel", L"ドット絵", L"像素", L"像素") \
    X(ST_NORMAL, L"Normal", L"通常", L"普通", L"一般") \
    X(ST_TRANSPARENT, L"transparent", L"透明", L"透明", L"透明") \
    X(COPIED, L"Copied %dx%d", L"コピーしました %dx%d", L"已复制 %dx%d", L"已複製 %dx%d") \
    X(DELETED, L"Deleted %s", L"削除しました %s", L"已删除 %s", L"已刪除 %s") \
    X(GONE, L"Already removed", L"すでに削除されています", L"已被删除", L"已被刪除") \
    X(MEMLIMIT, L"Memory limit: first %d frames", L"メモリ上限：最初の %d コマ", L"内存上限：仅前 %d 帧", L"記憶體上限：僅前 %d 格") \
    X(FRAMELIMIT, L"Frame limit: first %d frames", L"コマ数の上限：最初の %d コマ", L"帧数上限：仅前 %d 帧", L"格數上限：僅前 %d 格") \
    X(COPY_FAILED, L"Not enough memory to copy", L"メモリが足りずコピーできませんでした", L"内存不足，无法复制", L"記憶體不足，無法複製") \
    X(CANNOT_OPEN, L"Cannot open this file", L"このファイルは開けません", L"无法打开此文件", L"無法開啟此檔案") \
    X(FILTER_IMAGES, L"Images", L"画像", L"图像", L"影像") \
    X(FILTER_ALL, L"All files", L"すべてのファイル", L"所有文件", L"所有檔案") \
    /* 削除の確認と失敗 */ \
    X(DEL_TITLE, L"Delete", L"削除", L"删除", L"刪除") \
    X(CONFIRM_BIN, L"Move \"%s\" to the Recycle Bin?", L"「%s」をごみ箱に移動しますか？", L"要将“%s”移到回收站吗？", L"要將「%s」移到資源回收筒嗎？") \
    X(PERMANENT, L"\"%s\" will be deleted permanently.", L"「%s」は完全に削除されます。", L"“%s”将被永久删除。", L"「%s」將被永久刪除。") \
    X(NO_RESTORE, L"It cannot be restored.", L"元に戻せません。", L"无法恢复。", L"無法復原。") \
    X(RO_NOTE, L"It is read-only.", L"読み取り専用のファイルです。", L"这是只读文件。", L"這是唯讀檔案。") \
    X(RO_MAIN, L"\"%s\" is read-only.", L"「%s」は読み取り専用です。", L"“%s”是只读文件。", L"「%s」是唯讀檔案。") \
    X(RO_ASK, L"Move it to the Recycle Bin anyway?", L"それでもごみ箱に移動しますか？", L"仍要移到回收站吗？", L"仍要移到資源回收筒嗎？") \
    X(CANT_DELETE, L"Could not delete \"%s\".", L"「%s」を削除できませんでした。", L"无法删除“%s”。", L"無法刪除「%s」。") \
    X(IN_USE, L"It is being used by another program.", L"別のプログラムが使用中です。", L"其他程序正在使用该文件。", L"其他程式正在使用此檔案。") \
    X(DENIED, L"Access denied.", L"アクセスが拒否されました。", L"拒绝访问。", L"存取被拒。") \
    X(UNKNOWN_ERR, L"Unknown error (0x%08X)", L"不明なエラー（0x%08X）", L"未知错误（0x%08X）", L"不明錯誤（0x%08X）") \
    /* ボタン */ \
    X(BTN_YES, L"&Yes", L"はい(&Y)", L"是(&Y)", L"是(&Y)") \
    X(BTN_NO, L"&No", L"いいえ(&N)", L"否(&N)", L"否(&N)") \
    X(BTN_OK, L"OK", L"OK", L"确定", L"確定") \
    X(BTN_CANCEL, L"Cancel", L"キャンセル", L"取消", L"取消") \
    /* Settings */ \
    X(SET_TITLE, L"Settings", L"設定", L"设置", L"設定") \
    X(SET_DISPLAY, L"Display", L"表示", L"显示", L"顯示") \
    X(SET_PIXEL, L"Pixel mode", L"ドット絵モード", L"像素模式", L"像素模式") \
    X(SET_BG, L"Background:", L"背景：", L"背景：", L"背景：") \
    X(SET_CHECKER, L"Checker", L"チェック", L"棋盘格", L"棋盤格") \
    X(SET_BLACK, L"Black", L"黒", L"黑", L"黑") \
    X(SET_WHITE, L"White", L"白", L"白", L"白") \
    X(SET_CUSTOM, L"Custom", L"任意色", L"自定义", L"自訂") \
    X(SET_CHOOSE, L"Choose...", L"選択...", L"选择...", L"選擇...") \
    X(SET_DELETE, L"Delete", L"削除", L"删除", L"刪除") \
    X(SET_NOCONF, L"Delete without confirmation", L"確認せずに削除する", L"删除时不确认", L"刪除時不確認") \
    X(SET_RESETS, L"Resets to off every time TLIV starts.", L"TLIV を起動するたびに OFF に戻ります。", L"每次启动 TLIV 时恢复为关闭。", L"每次啟動 TLIV 時恢復為關閉。") \
    X(SET_LANG, L"Language", L"言語", L"语言", L"語言") \
    /* Keys と About（本文は桁揃えをしない。キーの表記はそのまま、説明だけ訳す） */ \
    X(KEYS_TITLE, L"Keys", L"キー操作", L"快捷键", L"快速鍵") \
    X(ABOUT_TITLE, L"About", L"バージョン情報", L"关于", L"關於") \
    X(KEYS_BODY, \
        L"Wheel / Ctrl+Wheel     Zoom at cursor\n" \
        L"Shift+Wheel / Left, Right   Previous / next image\n" \
        L"Left drag              Pan (Select mode: select area)\n" \
        L"S                      Select mode on / off\n" \
        L"Space / Click          Play / pause animation\n" \
        L"Q / E                  Previous / next frame\n" \
        L"Ctrl+A                 Select all\n" \
        L"Ctrl+C                 Copy selection (or whole image)\n" \
        L"Esc                    Deselect\n" \
        L"R                      Reset zoom and position\n" \
        L"B                      Background: checker / black / white / custom\n" \
        L"P                      Pixel mode / normal mode\n" \
        L"I                      Info panel\n" \
        L"Delete                 Move to Recycle Bin\n" \
        L"Ctrl+O                 Open...\n" \
        L"Ctrl+,                 Settings...", \
        L"ホイール / Ctrl+ホイール：カーソル位置を中心に拡大・縮小\n" \
        L"Shift+ホイール / ← →：前後の画像へ\n" \
        L"左ドラッグ：パン（範囲選択モード中は範囲選択）\n" \
        L"S：範囲選択モードの切り替え\n" \
        L"Space / クリック：アニメの再生・停止\n" \
        L"Q / E：前のコマ / 次のコマ\n" \
        L"Ctrl+A：すべて選択\n" \
        L"Ctrl+C：選択範囲（なければ画像全体）をコピー\n" \
        L"Esc：選択を解除\n" \
        L"R：倍率と位置を戻す\n" \
        L"B：背景の切り替え（チェック / 黒 / 白 / 任意色）\n" \
        L"P：ドット絵モード / 通常モード\n" \
        L"I：情報パネル\n" \
        L"Delete：ごみ箱へ移動\n" \
        L"Ctrl+O：開く...\n" \
        L"Ctrl+,：設定...", \
        L"滚轮 / Ctrl+滚轮：以光标位置为中心缩放\n" \
        L"Shift+滚轮 / ← →：上一张 / 下一张\n" \
        L"左键拖动：平移（选区模式中为选取区域）\n" \
        L"S：开启 / 关闭选区模式\n" \
        L"空格键 / 单击：播放 / 暂停动画\n" \
        L"Q / E：上一帧 / 下一帧\n" \
        L"Ctrl+A：全选\n" \
        L"Ctrl+C：复制选区（没有选区则复制整张图像）\n" \
        L"Esc：取消选择\n" \
        L"R：重置缩放和位置\n" \
        L"B：切换背景（棋盘格 / 黑 / 白 / 自定义）\n" \
        L"P：像素模式 / 普通模式\n" \
        L"I：信息面板\n" \
        L"Delete：移到回收站\n" \
        L"Ctrl+O：打开...\n" \
        L"Ctrl+,：设置...", \
        L"滾輪 / Ctrl+滾輪：以游標位置為中心縮放\n" \
        L"Shift+滾輪 / ← →：上一張 / 下一張\n" \
        L"左鍵拖曳：平移（選取模式中為選取範圍）\n" \
        L"S：開啟 / 關閉選取模式\n" \
        L"空白鍵 / 按一下：播放 / 暫停動畫\n" \
        L"Q / E：上一格 / 下一格\n" \
        L"Ctrl+A：全選\n" \
        L"Ctrl+C：複製選取範圍（沒有選取則複製整張影像）\n" \
        L"Esc：取消選取\n" \
        L"R：重設縮放與位置\n" \
        L"B：切換背景（棋盤格 / 黑 / 白 / 自訂）\n" \
        L"P：像素模式 / 一般模式\n" \
        L"I：資訊面板\n" \
        L"Delete：移到資源回收筒\n" \
        L"Ctrl+O：開啟...\n" \
        L"Ctrl+,：設定...")
