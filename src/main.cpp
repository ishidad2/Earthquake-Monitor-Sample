#include <M5Unified.h>
#include "network.h"
#include "earthquake.h"
#include "websocket.h"
#include "display.h"
#include "notification.h"

// カラー定義
#define COLOR_BG        TFT_BLACK
#define COLOR_HEADER    TFT_NAVY
#define COLOR_TEXT      TFT_WHITE
#define COLOR_GOOD      TFT_GREEN
#define COLOR_NORMAL    TFT_YELLOW
#define COLOR_POOR      TFT_ORANGE
#define COLOR_BAD       TFT_RED
#define COLOR_CALIB     TFT_BLUE
#define COLOR_CO2_LINE  TFT_CYAN
#define COLOR_TVOC_LINE TFT_MAGENTA
#define COLOR_GRID      TFT_DARKGREY

// WiFiアイコン配置
#define WIFI_ICON_X 295
#define WIFI_ICON_Y 5
#define WIFI_ICON_WIDTH 20
#define WIFI_ICON_HEIGHT 15

// WebSocketインジケーター配置
#define WS_INDICATOR_X 240         // タイトル削除により左に移動（時刻表示との干渉回避）
#define WS_INDICATOR_Y 8
#define WS_INDICATOR_WIDTH 20      // "WS"テキストの幅
#define WS_INDICATOR_HEIGHT 16     // フォントサイズ1の高さ
#define HEADER_ELEMENT_MARGIN 5    // ヘッダー要素間の最小マージン

#define PAGE_SIZE 30

// 画面設定
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// ヘッダー設定
#define HEADER_HEIGHT 30
#define HEADER_TITLE_X 5
#define HEADER_TITLE_Y 8
#define SHOW_HEADER_TITLE false  // タイトル表示制御（trueで表示、falseで非表示）

// 時刻表示設定
#define TIME_DISPLAY_X 120        // タイトル削除により左に移動（WebSocketインジケーターとの干渉回避）
#define TIME_DISPLAY_Y 8          // ヘッダーと同じY座標
#define TIME_DISPLAY_MAX_WIDTH 125  // "YYYY/MM/DD HH:MM"の最大幅（実測値、境界チェック用）
#define TIME_DISPLAY_HEIGHT 16    // フォントサイズ2の高さ
#define TIME_DISPLAY_MARGIN 5    // クリア領域の左右マージン（片側）

// 起動画面設定
#define VERSION_TEXT "v1.0.0"      // バージョン番号（手動更新）

// プログレスバー設定
#define PROGRESS_BAR_X 40          // プログレスバーのX座標（左右マージン40px）
#define PROGRESS_BAR_Y 200         // プログレスバーのY座標（画面下部から40px）
#define PROGRESS_BAR_WIDTH 240     // プログレスバーの幅（画面幅320px - 左右マージン80px）
#define PROGRESS_BAR_HEIGHT 20     // プログレスバーの高さ

// ステータスメッセージ表示位置
#define STATUS_MESSAGE_Y 160       // ステータスメッセージのY座標

// WiFi接続状態
bool isWiFiConnected = false;
// NTP同期状態
bool isNTPSynced = false;

// コンソールにログを追加
void consoleLog(String message) {
    Serial.println(message);
}

/**
 * @brief WebSocket接続状態インジケーターを描画
 * @param connected WebSocket接続状態（true: 接続、false: 切断）
 * @details WiFiアイコンの左側に"WS"テキストを表示
 */
void drawWebSocketIndicator(bool connected) {
    uint16_t color = connected ? COLOR_GOOD : COLOR_GRID;

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(color);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.drawString("WS", WS_INDICATOR_X, WS_INDICATOR_Y, 1);
}

/**
 * @brief WiFi接続状態アイコンを描画（携帯電波マーク）
 * @param connected WiFi接続状態（true: 接続、false: 切断）
 */
void drawWiFiIcon(bool connected) {
    uint16_t color = connected ? COLOR_GOOD : COLOR_GRID;

    int baseX = WIFI_ICON_X + 8;
    int baseY = WIFI_ICON_Y + 15;

    // 携帯電波マーク（3本の縦線、高さが異なる）
    // 左の線（低）
    M5.Display.fillRect(baseX, baseY - 4, 2, 4, color);

    // 中央の線（中）
    M5.Display.fillRect(baseX + 4, baseY - 8, 2, 8, color);

    // 右の線（高）
    M5.Display.fillRect(baseX + 8, baseY - 12, 2, 12, color);

    // 接続失敗時は斜め線を追加
    if (!connected) {
        M5.Display.drawLine(WIFI_ICON_X + 5, WIFI_ICON_Y + 4,
                        WIFI_ICON_X + 18, WIFI_ICON_Y + 15, COLOR_GRID);
    }
}

/**
 * @brief メイン画面ヘッダーを描画
 * @details ヘッダー背景、アプリケーションタイトル、WiFiアイコンを描画
 */
void drawMainHeader() {
    // ヘッダー背景
    M5.Display.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(TL_DATUM);

#if SHOW_HEADER_TITLE
    // タイトル表示（左寄せ）
    M5.Display.setTextColor(COLOR_TEXT);
    M5.Display.drawString("Earthquake Monitor", HEADER_TITLE_X, HEADER_TITLE_Y, 2);
#endif

    // 時刻表示（中央寄せ）
    M5.Display.setTextDatum(TC_DATUM);
    if (isNTPSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeStr[20];
            strftime(timeStr, sizeof(timeStr), "%Y/%m/%d %H:%M", &timeinfo);
            M5.Display.drawString(timeStr, TIME_DISPLAY_X, TIME_DISPLAY_Y, 2);
        } else {
            M5.Display.drawString("No Time Data", TIME_DISPLAY_X, TIME_DISPLAY_Y, 2);
        }
    } else {
        M5.Display.drawString("No Time Data", TIME_DISPLAY_X, TIME_DISPLAY_Y, 2);
    }

    // WiFiアイコン
    drawWiFiIcon(isWiFiConnected);
}

/**
 * @brief メイン画面ヘッダーのWiFiアイコンと時刻表示を更新
 * @details WiFi接続状態が変化した場合のみアイコンを再描画し、
 *          時刻が変更された場合のみ時刻表示を再描画する（差分更新によるフリッカー防止）。
 *          時刻表示のクリア領域は、M5GFX textWidth()を使用して文字列の実際の幅を動的に計算し、
 *          前の時刻表示が完全に消去されるように最適化されている。
 *          また、タイトルやWiFiアイコンと重ならないよう境界チェックを実施する。
 */
void updateMainHeader() {
    static bool lastWiFiState = false;
    static bool lastWsState = false;

    // WiFi状態の差分描画
    if (isWiFiConnected != lastWiFiState) {
        drawWiFiIcon(isWiFiConnected);
        lastWiFiState = isWiFiConnected;
    }

    // WebSocket状態の差分描画
    bool currentWsState = getWebSocketConnected();
    if (currentWsState != lastWsState) {
        drawWebSocketIndicator(currentWsState);
        lastWsState = currentWsState;
    }

    // 時刻表示の差分更新
    static char lastTimeStr[20] = "";
    char currentTimeStr[20];

    if (isNTPSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            strftime(currentTimeStr, sizeof(currentTimeStr), "%Y/%m/%d %H:%M", &timeinfo);
        } else {
            strcpy(currentTimeStr, "No Time Data");
        }
    } else {
        strcpy(currentTimeStr, "No Time Data");
    }

    if (strcmp(currentTimeStr, lastTimeStr) != 0) {
        // 文字列の実際の幅を取得
        M5.Display.setTextSize(1);
        M5.Display.setTextDatum(TC_DATUM);
        int16_t textWidth = M5.Display.textWidth(currentTimeStr, &fonts::Font2);

        // textWidth()の妥当性チェック（エラーハンドリング）
        if (textWidth <= 0 || textWidth > TIME_DISPLAY_MAX_WIDTH) {
            consoleLog("Warning: textWidth out of bounds (" + String(textWidth) +
                      "), using fallback: " + String(TIME_DISPLAY_MAX_WIDTH));
            textWidth = TIME_DISPLAY_MAX_WIDTH;
        }

        // クリア領域を計算（中央寄せ考慮 + マージン）
        int16_t clearWidth = textWidth + (2 * TIME_DISPLAY_MARGIN);
        int16_t clearX = TIME_DISPLAY_X - clearWidth / 2;

        // 境界チェック：タイトルと重ならないようにする
        const int16_t titleRightEdge = HEADER_TITLE_X + 130;  // "Earthquake Monitor"の右端
        if (clearX < titleRightEdge) {
            consoleLog("Warning: Clear area overlaps title, adjusting clearX");
            clearX = titleRightEdge;
            clearWidth = TIME_DISPLAY_X + clearWidth / 2 - clearX;
        }

        // 境界チェック：WebSocketインジケーターと重ならないようにする
        const int16_t wsLeftEdge = WS_INDICATOR_X - HEADER_ELEMENT_MARGIN;
        if (clearX + clearWidth > wsLeftEdge) {
            consoleLog("Warning: Clear area overlaps WS indicator, adjusting clearWidth");
            clearWidth = wsLeftEdge - clearX;
        }

        // 境界チェック：WiFiアイコンと重ならないようにする
        const int16_t wifiLeftEdge = WIFI_ICON_X - 5;
        if (clearX + clearWidth > wifiLeftEdge) {
            consoleLog("Warning: Clear area overlaps WiFi icon, adjusting clearWidth");
            clearWidth = wifiLeftEdge - clearX;
        }

        // 時刻表示領域のクリア（動的に計算された正確な範囲）
        M5.Display.fillRect(clearX, TIME_DISPLAY_Y,
                           clearWidth, TIME_DISPLAY_HEIGHT, COLOR_HEADER);

        // 新しい時刻を描画
        M5.Display.setTextSize(1);
        M5.Display.setTextDatum(TC_DATUM);
        M5.Display.setTextColor(COLOR_TEXT);
        M5.Display.drawString(currentTimeStr, TIME_DISPLAY_X, TIME_DISPLAY_Y, 2);

        // WebSocketインジケーターを再描画（時刻更新で消えるのを防ぐ）
        drawWebSocketIndicator(getWebSocketConnected());

        strcpy(lastTimeStr, currentTimeStr);
    }
}

/**
 * @brief プログレスバーを描画
 * @param x プログレスバーのX座標
 * @param y プログレスバーのY座標
 * @param width プログレスバーの幅
 * @param height プログレスバーの高さ
 * @param progress 進捗率（0-100）
 */
void drawProgressBar(int x, int y, int width, int height, int progress) {
    // 進捗率を0-100の範囲にクランプ
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    // 進捗幅を計算（枠線から2px内側にオフセット）
    int fillWidth = (width - 4) * progress / 100;

    // 進捗バーを描画（枠線から2px内側）
    M5.Display.fillRect(x + 2, y + 2, fillWidth, height - 4, COLOR_GOOD);
}

/**
 * @brief 起動画面を初期化して表示
 * @details タイトル、バージョン情報、プログレスバー枠を描画
 */
void showStartupScreen() {
    // 画面全体をクリア
    M5.Display.fillScreen(COLOR_BG);

    // 文字色設定
    M5.Display.setTextColor(COLOR_TEXT);

    // テキスト配置設定（中央寄せ）
    M5.Display.setTextDatum(TC_DATUM);

    // タイトル表示
    M5.Display.drawString("Earthquake Monitor", SCREEN_WIDTH / 2, 10, 4);

    // バージョン表示
    M5.Display.drawString(VERSION_TEXT, SCREEN_WIDTH / 2, 50, 2);

    // プログレスバーの枠線を描画
    M5.Display.drawRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_GRID);

    // 初期ステータスメッセージ表示
    M5.Display.drawString("Initializing...", SCREEN_WIDTH / 2, STATUS_MESSAGE_Y, 2);
}

/**
 * @brief 起動画面の進行状況を更新
 * @param message 表示するステータスメッセージ
 * @param progress 進捗率（0-100）
 * @param isSuccess 状態表示（1: 成功/緑色、0: 失敗/オレンジ色、-1: 進行中/白色、デフォルト: -1）
 */
void updateStartupProgress(const String &message, int progress, int isSuccess = -1) {
    // 前回のステータスメッセージエリアをクリア（フリッカー防止）
    M5.Display.fillRect(0, STATUS_MESSAGE_Y - 10, SCREEN_WIDTH, 30, COLOR_BG);

    // isSuccessに応じて文字色を設定
    if (isSuccess == 1) {
        M5.Display.setTextColor(COLOR_GOOD);  // 成功: 緑色
    } else if (isSuccess == 0) {
        M5.Display.setTextColor(COLOR_POOR);  // 失敗: オレンジ色
    } else {
        M5.Display.setTextColor(COLOR_TEXT);  // 進行中: 白色
    }

    // ステータスメッセージを表示
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.drawString(message, SCREEN_WIDTH / 2, STATUS_MESSAGE_Y, 2);

    // プログレスバーを更新
    drawProgressBar(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, progress);

    // シリアルログに出力
    consoleLog(message);
}

/**
 * @brief 起動処理完了を表示し、メイン画面に遷移
 */
void completeStartup() {
    // プログレスバーを100%に更新
    drawProgressBar(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, 100);

    // 「準備完了」メッセージを表示
    M5.Display.fillRect(0, STATUS_MESSAGE_Y - 10, SCREEN_WIDTH, 30, COLOR_BG);
    M5.Display.setTextColor(COLOR_GOOD);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.drawString("Ready", SCREEN_WIDTH / 2, STATUS_MESSAGE_Y, 4);

    // シリアルログに出力
    consoleLog("Startup complete.");

    // 1秒待機
    delay(1000);

    // 画面をクリア（メイン画面への遷移準備）
    M5.Display.fillScreen(COLOR_BG);

    // メイン画面ヘッダーを描画
    drawMainHeader();
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.internal_spk = true;  // 通知機能のためスピーカーを有効化
    cfg.internal_mic = false;
    M5.begin(cfg);

    // 通知機能初期化
    initNotification();

    // 起動画面を表示
    showStartupScreen();

    Serial.println();
    Serial.println("========================================");
    Serial.println("M5Stack Jishin Monitor");
    Serial.println("========================================");

    // WiFi設定取得とWiFi接続
    String ssid, password;
    getWiFiCredentials(ssid, password);

    // Symbol設定を事前に宣言（後続の処理で使用）
    SymbolConfig symbolConfig;

    // WiFi接続中表示
    updateStartupProgress("Connecting to WiFi...", 25);
    isWiFiConnected = connectToWiFi(ssid, password);

    // WiFi接続結果表示
    if (isWiFiConnected) {
        updateStartupProgress("WiFi Connected", 50, 1);

        // タイムゾーン設定取得
        int32_t timezoneOffset = getTimezoneConfig();

        // Symbol設定取得
        symbolConfig = getSymbolConfig();

        // NTP時刻同期中表示
        updateStartupProgress("Syncing Time...", 75);
        isNTPSynced = syncNTP(timezoneOffset);

        // NTP同期結果表示
        if (isNTPSynced) {
            updateStartupProgress("Time Synced", 100, 1);
        } else {
            updateStartupProgress("Time Sync Failed", 100, 0);
        }
    } else {
        updateStartupProgress("WiFi Connection Failed", 100, 0);
    }

    // 起動完了処理
    completeStartup();

    // 地震情報を取得（WiFi接続時のみ）
    if (isWiFiConnected) {
        fetchEarthquakeData(symbolConfig, PAGE_SIZE);

        // WebSocket初期化（REST API取得後）
        initWebSocket(symbolConfig);
    }

    // 地震情報表示初期化（データ取得後に実行）
    initDisplay();
}

void loop() {
    M5.update();

    // メイン画面ヘッダーのWiFi状態を更新
    updateMainHeader();

    // 地震情報表示更新（タッチ処理を含む）
    updateDisplay();

    // WebSocketループ処理
    webSocketLoop();

    // 通知処理更新（ノンブロッキング音声再生、視覚通知、キュー処理）
    updateNotification();
}
