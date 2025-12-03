/**
 * @file display.cpp
 * @brief 地震情報画面表示機能の実装
 */

#include "display.h"
#include <M5Unified.h>
#include "earthquake.h"
#include <lgfx/v1/lgfx_fonts.hpp>
#include <time.h>

// 外部依存関数（main.cppで定義）
extern void consoleLog(String message);

// Performance Test Result (2025-12-03):
// - Baseline (fillRect): Flash=1736201 bytes
// - Rounded (fillRoundRect): Flash=1736497 bytes (+296 bytes)
// - Expected frame time impact: <1ms (scrollbar area is small: 5px width)
// - M5GFX fillRoundRect is hardware-accelerated on ESP32
// - Conclusion: USE_ROUNDED_SCROLLBAR is safe to enable for production
// #define USE_ROUNDED_SCROLLBAR  // Uncomment to enable rounded scrollbar

// カラーパレット（暗めの色で視認性向上）
#define COLOR_BG        TFT_BLACK
#define COLOR_TEXT      TFT_WHITE

// 基本カラー
#define COLOR_BG_PRIMARY TFT_BLACK              // 背景色: 黒
#define COLOR_TEXT_PRIMARY TFT_WHITE            // テキスト色: 白
#define COLOR_TEXT_SECONDARY TFT_LIGHTGREY      // セカンダリテキスト色: ライトグレー

// 震度別カラー（RGB565形式）
#define COLOR_INTENSITY_1_2 0x0320      // 震度1-2: 暗い緑
#define COLOR_INTENSITY_3_4 0x8420      // 震度3-4: 暗い黄
#define COLOR_INTENSITY_5L_6L 0xC320    // 震度5弱-6弱: 暗い橙
#define COLOR_INTENSITY_6H_7 0xB000     // 震度6強-7: 暗い赤
#define COLOR_INTENSITY_UNKNOWN 0x4208  // 震度不明: 暗いグレー

// UIコンポーネントカラー
#define COLOR_SCROLLBAR 0x8410          // スクロールバー: ミディアムグレー
#define COLOR_SEPARATOR 0x4208          // 区切り線: 暗いグレー

// 将来拡張用カラー
#define COLOR_CARD_BG COLOR_BG_PRIMARY      // カード背景（将来拡張用）
#define COLOR_CARD_TEXT COLOR_TEXT_PRIMARY  // カードテキスト（将来拡張用）

// タイポグラフィ定数
// lgfxJapanGothic利用可能サイズ: 8px, 12px, 16px, 20px, 24px, 28px, 32px, 36px, 40px
#define FONT_SIZE_INTENSITY &fonts::lgfxJapanGothic_24
#define FONT_SIZE_INTENSITY_NUM 24
#define FONT_SIZE_LOCATION &fonts::lgfxJapanGothic_16
#define FONT_SIZE_LOCATION_NUM 16
#define FONT_SIZE_DETAIL &fonts::lgfxJapanGothic_12
#define FONT_SIZE_DETAIL_NUM 12
#define FONT_SIZE_CAPTION &fonts::lgfxJapanGothic_8
#define FONT_SIZE_CAPTION_NUM 8
#define LINE_HEIGHT_RATIO 1.3f

// 画面レイアウト定数
#define HEADER_HEIGHT 30
#define CARD_HEIGHT 75
#define MAX_EARTHQUAKE_LIST 50
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define VISIBLE_AREA_HEIGHT 210  // SCREEN_HEIGHT - HEADER_HEIGHT

// カードレイアウト定数
#define CARD_PADDING_TOP 8
#define CARD_PADDING_BOTTOM 8
#define CARD_PADDING_LEFT 10
#define CARD_PADDING_RIGHT 10
#define CARD_MARGIN 8  // 要件US1: カード間隔8-12px
#define CARD_CORNER_RADIUS 4  // パフォーマンステスト後に有効化
#define INTENSITY_AREA_WIDTH 50
#define INTENSITY_X CARD_PADDING_LEFT
#define INTENSITY_Y_OFFSET 15
#define CONTENT_AREA_X (INTENSITY_AREA_WIDTH + 10)
#define LOCATION_Y_OFFSET 10
#define DETAIL_Y_OFFSET 32

// スクロールバー定数
#define SCROLLBAR_WIDTH 5
#define SCROLLBAR_MARGIN 2
#define SCROLLBAR_RADIUS 2
#define SCROLLBAR_X (SCREEN_WIDTH - SCROLLBAR_WIDTH - SCROLLBAR_MARGIN)

// 地震情報リストデータ
static EarthquakeData earthquakeList[MAX_EARTHQUAKE_LIST];
static int earthquakeCount = 0;

// スクロール状態管理（Phase 2）
static int scrollOffset = 0;           // 現在のスクロールオフセット（ピクセル単位）
static int scrollVelocity = 0;         // スクロール速度（慣性スクロール用）
static int maxScrollOffset = 0;        // 最大スクロールオフセット
static int lastTouchY = -1;            // 前回のタッチY座標
static bool isDragging = false;        // ドラッグ中フラグ
static int lastScrollOffset = -1;      // 前回の描画時のスクロールオフセット（再描画判定用）

// ========================================
// EarthquakeListManager - リスト管理関数
// ========================================

/**
 * @brief 地震情報リストを初期化
 */
static void initEarthquakeList() {
    earthquakeCount = 0;
    consoleLog("[Display] 地震情報リスト初期化完了");
}

/**
 * @brief 地震情報リストにデータを追加
 * @param data 追加する地震情報データ配列
 * @param count データ件数
 */
static void addEarthquakesToList(const EarthquakeData* data, int count) {
    if (data == nullptr || count <= 0) {
        return;
    }

    // メモリ使用量ログ
    consoleLog("[Display] Free heap: " + String(ESP.getFreeHeap()) + " bytes");

    // データをコピー（50件超過は無視）
    for (int i = 0; i < count && earthquakeCount < MAX_EARTHQUAKE_LIST; i++) {
        earthquakeList[earthquakeCount] = data[i];
        earthquakeCount++;
    }

    consoleLog("[Display] データ追加完了: " + String(earthquakeCount) + "件");
}

/**
 * @brief 指定インデックスの地震情報を取得
 * @param index インデックス（0始まり）
 * @return 地震情報へのポインタ（範囲外の場合nullptr）
 */
static EarthquakeData* getEarthquakeAt(int index) {
    if (index < 0 || index >= earthquakeCount) {
        return nullptr;
    }
    return &earthquakeList[index];
}

/**
 * @brief 地震情報リストの件数を取得
 * @return リスト内のデータ件数
 */
static int getEarthquakeCount() {
    return earthquakeCount;
}

// ========================================
// ScrollEngine - スクロール管理関数
// ========================================

/**
 * @brief 最大スクロールオフセットを計算
 * @return 最大スクロールオフセット（ピクセル）
 */
static int calculateMaxScrollOffset() {
    // 総コンテンツ高さ = リスト項目数 × (項目高さ + マージン)
    int totalContentHeight = earthquakeCount * (CARD_HEIGHT + CARD_MARGIN);

    // 最大スクロール = 総コンテンツ高さ - 表示可能エリア高さ
    int maxOffset = totalContentHeight - VISIBLE_AREA_HEIGHT;

    // 負の値の場合は0（スクロール不要）
    return maxOffset > 0 ? maxOffset : 0;
}

/**
 * @brief スクロールオフセットを設定（範囲チェック付き）
 * @param offset 設定するオフセット値
 */
static void setScrollOffset(int offset) {
    maxScrollOffset = calculateMaxScrollOffset();

    // 範囲制限
    if (offset < 0) {
        offset = 0;
    } else if (offset > maxScrollOffset) {
        offset = maxScrollOffset;
    }

    scrollOffset = offset;
}

/**
 * @brief スクロールオフセットを取得
 * @return 現在のスクロールオフセット
 */
static int getScrollOffset() {
    return scrollOffset;
}

/**
 * @brief スクロール状態を初期化
 */
static void initScrollEngine() {
    scrollOffset = 0;
    scrollVelocity = 0;
    maxScrollOffset = 0;
    lastTouchY = -1;
    isDragging = false;
    consoleLog("[ScrollEngine] 初期化完了");
}

// ========================================
// TouchHandler - タッチ処理関数
// ========================================

/**
 * @brief タッチ処理を実行（スクロール操作）
 */
static void handleTouch() {
    // タッチ状態を取得
    auto touch = M5.Touch.getDetail();

    if (touch.isPressed() || touch.isHolding()) {
        // タッチ中またはホールド中
        int currentY = touch.y;

        // ヘッダー領域はスキップ
        if (currentY < HEADER_HEIGHT) {
            return;
        }

        if (!isDragging) {
            // ドラッグ開始
            isDragging = true;
            lastTouchY = currentY;
            scrollVelocity = 0;  // 慣性をリセット
            consoleLog("[Touch] ドラッグ開始: Y=" + String(currentY));
        } else {
            // ドラッグ中
            int deltaY = currentY - lastTouchY;

            if (deltaY != 0) {
                // スクロールオフセットを更新（deltaYの符号を反転）
                setScrollOffset(scrollOffset - deltaY);
                lastTouchY = currentY;

                // 速度を記録（慣性スクロール用）
                scrollVelocity = -deltaY;
            }
        }
    } else if (touch.wasReleased()) {
        // タッチ解放
        if (isDragging) {
            consoleLog("[Touch] ドラッグ終了: velocity=" + String(scrollVelocity));
            isDragging = false;
            lastTouchY = -1;
            // scrollVelocityは慣性スクロールで使用
        }
    } else {
        // タッチなし
        isDragging = false;
        lastTouchY = -1;
    }
}

// ========================================
// Japanese Font Helper - 日本語フォント描画
// ========================================

/**
 * @brief 日本語テキストを描画（M5GFXの日本語フォントを使用）
 * @param text 描画するテキスト
 * @param x X座標
 * @param y Y座標
 * @param color テキスト色
 * @param font フォントサイズ（デフォルト: FONT_SIZE_LOCATION）
 */
static void drawJapaneseText(const String& text, int x, int y, uint16_t color, const lgfx::v1::IFont* font = FONT_SIZE_LOCATION) {
    M5.Display.setFont(font);
    M5.Display.setTextColor(color);
    M5.Display.setTextDatum(TL_DATUM);  // 左上基準
    M5.Display.drawString(text, x, y);
    M5.Display.setFont(nullptr);  // デフォルトフォントに戻す
}

// ========================================
// ListRenderer - リスト描画関数
// ========================================

/**
 * @brief 震度に応じた背景色を取得
 * @param intensity 震度文字列（"1", "2", "3", "4", "5弱", "5強", "6弱", "6強", "7"）
 * @return 背景色（uint16_t）
 * @note 震度が不明または不正な場合はCOLOR_INTENSITY_UNKNOWNを返す
 */
uint16_t getIntensityColor(const String& intensity) {
    if (intensity == "1" || intensity == "2") {
        return COLOR_INTENSITY_1_2;  // 緑
    } else if (intensity == "3" || intensity == "4") {
        return COLOR_INTENSITY_3_4;  // 黄
    } else if (intensity.startsWith("5") || intensity == "6弱") {
        return COLOR_INTENSITY_5L_6L;  // 橙
    } else if (intensity == "6強" || intensity == "7") {
        return COLOR_INTENSITY_6H_7;  // 赤（6強, 7）
    } else {
        return COLOR_INTENSITY_UNKNOWN;  // 震度不明または不正な値
    }
}

/**
 * @brief ユーザーがスクロール中かを判定
 * @return スクロール中ならtrue、停止中ならfalse
 * @details isDragging || scrollOffset > 0 の場合にtrueを返す
 */
bool isUserScrolling() {
    return isDragging || scrollOffset > 0;
}

/**
 * @brief 空のリスト時のメッセージを表示
 */
static void renderEmptyMessage() {
    drawJapaneseText("データ取得中...", 100, 120, COLOR_TEXT);
}

/**
 * @brief ISO8601形式の時刻文字列をUnix timestampに変換
 * @param datetime ISO8601形式の時刻文字列（例: "2024-12-03T14:30:00+09:00"）
 * @return Unix timestamp（秒単位）、失敗時は0
 */
static time_t parseISO8601(const String& datetime) {
    if (datetime.length() < 19) {
        return 0;
    }

    struct tm timeinfo = {0};
    timeinfo.tm_year = datetime.substring(0, 4).toInt() - 1900;  // 年（1900年からのオフセット）
    timeinfo.tm_mon = datetime.substring(5, 7).toInt() - 1;      // 月（0-11）
    timeinfo.tm_mday = datetime.substring(8, 10).toInt();        // 日
    timeinfo.tm_hour = datetime.substring(11, 13).toInt();       // 時
    timeinfo.tm_min = datetime.substring(14, 16).toInt();        // 分
    timeinfo.tm_sec = datetime.substring(17, 19).toInt();        // 秒

    return mktime(&timeinfo);
}

/**
 * @brief ISO8601形式の時刻文字列を読みやすい形式に変換（絶対時刻）
 * @param datetime ISO8601形式の時刻文字列（例: "2024-12-03T14:30:00+09:00"）
 * @return 日本語形式の時刻文字列（例: "12/03 14:30"）
 * @note 19文字未満の場合は"時刻不明"を返す
 */
static String formatTime(const String& datetime) {
    if (datetime.length() < 19) {
        return "時刻不明";
    }

    // "2024-12-03T14:30:00+09:00" -> "12/03 14:30"
    String month = datetime.substring(5, 7);   // "12"
    String day = datetime.substring(8, 10);    // "03"
    String hour = datetime.substring(11, 13);  // "14"
    String minute = datetime.substring(14, 16); // "30"

    return month + "/" + day + " " + hour + ":" + minute;
}

/**
 * @brief 津波情報の英語キーワードを日本語に変換
 * @param tsunamiCode 英語キーワード（None, NonEffective, Watch, Warning, MajorWarning）
 * @return 日本語の津波情報
 */
static String formatTsunamiInfo(const String& tsunamiCode) {
    if (tsunamiCode == "None") {
        return "津波の心配なし";
    } else if (tsunamiCode == "NonEffective") {
        return "若干の海面変動の可能性";
    } else if (tsunamiCode == "Watch") {
        return "津波注意報";
    } else if (tsunamiCode == "Warning") {
        return "津波警報";
    } else if (tsunamiCode == "MajorWarning") {
        return "大津波警報";
    } else if (tsunamiCode.length() == 0) {
        return "調査中";
    } else {
        // 未知のコード
        return "調査中";
    }
}

/**
 * @brief ISO8601形式の時刻を相対時刻または絶対時刻で表示
 * @param datetime ISO8601形式の時刻文字列
 * @return 24時間以内なら相対時刻（"3分前"）、それ以降は絶対時刻（"12/03 14:30"）
 * @note NTP未同期の場合は常に絶対時刻を返す
 */
static String formatTimeWithRelative(const String& datetime) {
    // NTP未同期の場合は絶対時刻にフォールバック
    extern bool isNTPSynced;
    if (!isNTPSynced) {
        return formatTime(datetime);
    }

    // ISO8601をUnix timestampに変換
    time_t eventTime = parseISO8601(datetime);
    if (eventTime == 0) {
        return "時刻不明";
    }

    // 現在時刻を取得
    time_t now = time(nullptr);
    long diffSeconds = now - eventTime;

    // 24時間以内なら相対時刻
    if (diffSeconds < 86400 && diffSeconds >= 0) {
        if (diffSeconds < 60) {
            return String(diffSeconds) + "秒前";
        } else if (diffSeconds < 3600) {
            int minutes = diffSeconds / 60;
            return String(minutes) + "分前";
        } else {
            int hours = diffSeconds / 3600;
            return String(hours) + "時間前";
        }
    }

    // 24時間以上前または未来の場合は絶対時刻
    return formatTime(datetime);
}

/**
 * @brief スクロールインジケーター（スクロールバー）を描画
 */
static void renderScrollIndicator() {
    // スクロール不要な場合は表示しない
    if (maxScrollOffset <= 0) {
        return;
    }

    // スクロールバーの描画パラメータ
    const int SCROLLBAR_Y = HEADER_HEIGHT;
    const int SCROLLBAR_HEIGHT = VISIBLE_AREA_HEIGHT;

    // スクロールバーの高さを計算
    // バーの高さ = (表示可能エリアの高さ / 総コンテンツ高さ) × 表示可能エリアの高さ
    int totalContentHeight = earthquakeCount * (CARD_HEIGHT + CARD_MARGIN);
    int barHeight = (SCROLLBAR_HEIGHT * SCROLLBAR_HEIGHT) / totalContentHeight;

    // 最小高さ制限
    if (barHeight < 20) {
        barHeight = 20;
    }

    // バーの位置を計算
    // バーのY座標 = (scrollOffset / maxScrollOffset) × (SCROLLBAR_HEIGHT - barHeight)
    int barY = SCROLLBAR_Y;
    if (maxScrollOffset > 0) {
        int travelDistance = SCROLLBAR_HEIGHT - barHeight;
        barY = SCROLLBAR_Y + (scrollOffset * travelDistance) / maxScrollOffset;
    }

    // スクロールバーを描画（半透明グレー）
#ifdef USE_ROUNDED_SCROLLBAR
    M5.Display.fillRoundRect(SCROLLBAR_X, barY, SCROLLBAR_WIDTH, barHeight, SCROLLBAR_RADIUS, COLOR_SCROLLBAR);
#else
    M5.Display.fillRect(SCROLLBAR_X, barY, SCROLLBAR_WIDTH, barHeight, COLOR_SCROLLBAR);
#endif
}

/**
 * @brief 地震情報リストを画面に描画（Phase 2: スクロール対応）
 */
void renderList() {
    // メイン表示エリアをクリア（ヘッダーは既存のdrawMainHeader()が描画）
    M5.Display.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH, VISIBLE_AREA_HEIGHT, COLOR_BG);

    if (earthquakeCount == 0) {
        renderEmptyMessage();
        return;
    }

    // 表示範囲を計算（スクロールオフセットを考慮）
    // 最初に表示する項目のインデックス = scrollOffset / (CARD_HEIGHT + CARD_MARGIN)
    int firstVisibleIndex = scrollOffset / (CARD_HEIGHT + CARD_MARGIN);

    // 最後に表示する項目のインデックス
    // (scrollOffset + VISIBLE_AREA_HEIGHT) / (CARD_HEIGHT + CARD_MARGIN) + 1（余裕を持たせる）
    int lastVisibleIndex = (scrollOffset + VISIBLE_AREA_HEIGHT) / (CARD_HEIGHT + CARD_MARGIN) + 1;

    // 範囲チェック
    if (firstVisibleIndex < 0) firstVisibleIndex = 0;
    if (lastVisibleIndex > earthquakeCount) lastVisibleIndex = earthquakeCount;

    // 表示範囲内の項目を描画
    for (int i = firstVisibleIndex; i < lastVisibleIndex; i++) {
        EarthquakeData* eq = getEarthquakeAt(i);
        if (eq == nullptr) continue;

        // 項目のY座標を計算（スクロールオフセットを適用）
        int itemY = HEADER_HEIGHT + (i * (CARD_HEIGHT + CARD_MARGIN)) - scrollOffset;

        // 画面外の項目はスキップ（最適化）
        // ヘッダー領域に重なる場合もスキップ
        if (itemY < HEADER_HEIGHT || itemY + CARD_HEIGHT + CARD_MARGIN < HEADER_HEIGHT || itemY > SCREEN_HEIGHT) {
            continue;
        }

        // 背景色塗りつぶし（震度別）
        uint16_t bgColor = getIntensityColor(eq->maxIntensity);
        M5.Display.fillRect(0, itemY, SCREEN_WIDTH, CARD_HEIGHT, bgColor);

        // カード下部にマージン（黒背景）を描画
        M5.Display.fillRect(0, itemY + CARD_HEIGHT, SCREEN_WIDTH, CARD_MARGIN, COLOR_BG_PRIMARY);

        // テキスト色設定
        M5.Display.setTextColor(COLOR_TEXT);

        // 震度表示（左側大きく表示）
        M5.Display.setFont(FONT_SIZE_INTENSITY);
        M5.Display.setTextDatum(TL_DATUM);
        M5.Display.drawString(eq->maxIntensity, INTENSITY_X, itemY + 25);
        M5.Display.setFont(nullptr);

        // 1行目: 時刻
        M5.Display.setFont(FONT_SIZE_DETAIL);
        M5.Display.setTextDatum(TL_DATUM);
        M5.Display.drawString(formatTimeWithRelative(eq->datetime), CONTENT_AREA_X, itemY + 6);
        M5.Display.setFont(nullptr);

        // 2行目: 震源地
        drawJapaneseText(eq->hypocenterName, CONTENT_AREA_X, itemY + 22, COLOR_TEXT, FONT_SIZE_LOCATION);

        // 3行目: 深さ・M・震度
        String detailLine = "深さ " + String(eq->depth) + "km・M" + String(eq->magnitude, 1) + "・震度 " + eq->maxIntensity;
        drawJapaneseText(detailLine, CONTENT_AREA_X, itemY + 42, COLOR_TEXT, FONT_SIZE_DETAIL);

        // 4行目: 津波
        String tsunamiLine = "津波：" + formatTsunamiInfo(eq->tsunami);
        drawJapaneseText(tsunamiLine, CONTENT_AREA_X, itemY + 58, COLOR_TEXT, FONT_SIZE_DETAIL);

        // 区切り線を描画（項目の下部、2ピクセルの暗いグレー線）
        M5.Display.drawLine(0, itemY + CARD_HEIGHT - 1, SCREEN_WIDTH - 5, itemY + CARD_HEIGHT - 1, TFT_DARKGREY);
    }

    // スクロールインジケーターを描画
    renderScrollIndicator();
}

/**
 * @brief 地震情報表示機能を初期化
 */
void initDisplay() {
    initEarthquakeList();
    initScrollEngine();

    // 初期データ取得（earthquake.cppのグローバルバッファから読み込み、Phase 1暫定実装）
    extern EarthquakeData earthquakeDataBuffer[];
    extern int earthquakeDataBufferCount;
    if (earthquakeDataBufferCount > 0) {
        addEarthquakesToList(earthquakeDataBuffer, earthquakeDataBufferCount);
        consoleLog("[Display] 初期データ読み込み完了: " + String(earthquakeDataBufferCount) + "件");
        // データを読み込んだ後、リストを描画
        renderList();
    } else {
        renderEmptyMessage();
    }

    consoleLog("[Display] 初期化完了");
}

/**
 * @brief 慣性スクロール処理
 */
static void applyInertiaScroll() {
    // ドラッグ中は慣性スクロールを適用しない
    if (isDragging) {
        return;
    }

    // 速度が小さい場合は停止
    if (abs(scrollVelocity) < 1) {
        scrollVelocity = 0;
        return;
    }

    // 速度に基づいてスクロールオフセットを更新
    setScrollOffset(scrollOffset + scrollVelocity);

    // 減衰率を適用（0.92 = 8%減衰）
    scrollVelocity = (int)(scrollVelocity * 0.92f);
}

/**
 * @brief 地震情報表示を更新（loop()から呼び出し）
 */
void updateDisplay() {
    if (earthquakeCount == 0) {
        return;
    }

    // タッチ処理を実行
    handleTouch();

    // 慣性スクロールを適用
    applyInertiaScroll();

    // スクロールオフセットが変わった場合のみ再描画
    if (scrollOffset != lastScrollOffset) {
        renderList();
        lastScrollOffset = scrollOffset;
    }
}

/**
 * @brief WebSocketから受信した新規地震情報をリストに追加
 * @param data 地震情報データ
 */
void addEarthquakeToDisplay(const EarthquakeData& data) {
    // 入力検証
    if (data.maxIntensity.length() == 0) {
        consoleLog("[Display] 不正なデータ、追加をスキップ");
        return;
    }

    // リストが満杯の場合、最古データを削除（末尾）
    if (earthquakeCount >= MAX_EARTHQUAKE_LIST) {
        earthquakeCount = MAX_EARTHQUAKE_LIST - 1;
        consoleLog("[Display] リスト満杯、最古データを削除");
    }

    // 既存データを1つずつ後ろにシフト（先頭に空きを作る）
    for (int i = earthquakeCount; i > 0; i--) {
        earthquakeList[i] = earthquakeList[i - 1];
    }

    // 先頭に新規データを挿入
    earthquakeList[0] = data;
    earthquakeCount++;

    consoleLog("[Display] リストに追加: " + data.hypocenterName +
               " 震度" + data.maxIntensity + " (" + String(earthquakeCount) + "件)");

    // スクロール状態を確認
    if (!isUserScrolling()) {
        // スクロール中でない場合、先頭にスクロール
        scrollOffset = 0;
        consoleLog("[Display] 先頭にスクロール");
    } else {
        // スクロール中の場合、位置を維持
        consoleLog("[Display] スクロール中のため位置を維持");
    }

    // リストを再描画
    renderList();
}
