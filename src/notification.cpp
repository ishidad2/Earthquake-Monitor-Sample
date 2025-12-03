/**
 * @file notification.cpp
 * @brief 地震通知機能の実装
 */

#include "notification.h"
#include <M5Unified.h>

// 外部依存関数（main.cppで定義）
extern void consoleLog(String message);

// スピーカー状態
static bool isSpeakerEnabled = false;  // M5.Speaker初期化済みフラグ

// 通知キュー（連続地震対応）
#define NOTIFICATION_QUEUE_SIZE 3
static EarthquakeData notificationQueue[NOTIFICATION_QUEUE_SIZE];
static int queueHead = 0;  // キューの先頭インデックス（取り出し位置）
static int queueTail = 0;  // キューの末尾インデックス（追加位置）
static int queueCount = 0; // キュー内の通知数（0-3）

// 音声通知定数
static const int BEEP_DURATION_MS = 150;  // ビープ音の長さ（ミリ秒）
static const int BEEP_INTERVAL_MS = 100;  // ビープ音の間隔（ミリ秒）
static const int BEEP_FREQUENCY = 1000;   // ビープ音の周波数（Hz）
static const int BEEP_VOLUME = 96;        // ビープ音の音量（0-255）

// 音声通知状態
static int beepCount = 0;              // 再生すべきビープ音の回数（0=停止）
static int beepPlayed = 0;             // 再生済みビープ音の回数
static unsigned long lastBeepTime = 0; // 最後のビープ音再生時刻（millis）

// 視覚通知定数
static const int FLASH_DURATION_MS = 1500; // 点滅継続時間（ミリ秒）
static const int FLASH_INTERVAL_MS = 300;  // 点滅間隔（ミリ秒、ON/OFF切り替え）
static const uint16_t COLOR_BG = 0x0000;    // 通常の背景色（黒）

// 画面レイアウト定数（display.cppと同じ値）
static const int HEADER_HEIGHT = 30;
static const int SCREEN_WIDTH = 320;
static const int VISIBLE_AREA_HEIGHT = 210;  // SCREEN_HEIGHT - HEADER_HEIGHT

// 視覚通知状態
static bool isFlashing = false;         // 画面点滅中フラグ
static unsigned long flashStartTime = 0; // 点滅開始時刻（millis）
static uint16_t flashColor = 0;         // 点滅色（RGB565）

// 外部関数宣言（display.h/cppで定義、Task 5-8で実装）
extern uint16_t getIntensityColor(const String& intensity);
extern void renderList();
extern void addEarthquakeToDisplay(const EarthquakeData& data);

// 外部関数宣言（main.cppで定義）
extern void drawMainHeader();

// 前方宣言
static void processNotificationQueue();
static int getBeepCountForIntensity(const String& intensity);
static void playBeepSound(int count);
static void flashScreen(uint16_t color);
static void updateFlashScreen();

/**
 * @brief 通知機能を初期化（M5.Speaker初期化、状態変数リセット）
 * @details setup()から呼び出す。M5.begin()実行後に呼び出すこと
 */
void initNotification() {
    consoleLog("[Notification] 初期化開始");

    // スピーカー初期化試行
    if (!M5.Speaker.begin()) {
        consoleLog("[Notification] スピーカー初期化失敗、音声通知は無効化されます");
        isSpeakerEnabled = false;
        return;  // 早期リターン
    }

    isSpeakerEnabled = true;
    M5.Speaker.setVolume(96);  // 推奨64-128の中間値
    consoleLog("[Notification] スピーカー初期化成功、音量=96");

    // 通知キューの初期化
    queueHead = 0;
    queueTail = 0;
    queueCount = 0;

    consoleLog("[Notification] 初期化完了");
}

/**
 * @brief 新規地震情報を通知キューに追加
 * @param data 地震情報データ
 * @details WebSocketメッセージ受信時に呼び出す。キューに追加し、順次処理を行う
 * @note 重複検出はwebsocket.cpp層で完了済みと想定
 */
void notifyEarthquake(const EarthquakeData& data) {
    // メモリチェック（メモリ不足時は通知をスキップ）
    if (ESP.getFreeHeap() < 15000) {
        consoleLog("[Notification] メモリ不足により通知をスキップ (Free heap: " + String(ESP.getFreeHeap()) + " bytes)");
        return;
    }

    // 入力検証
    if (data.maxIntensity.length() == 0) {
        consoleLog("[Notification] 震度データが不正、通知をスキップ");
        return;
    }

    // キューが満杯の場合、最古の通知を破棄
    if (queueCount >= NOTIFICATION_QUEUE_SIZE) {
        consoleLog("[Notification] キュー満杯、最古の通知を破棄");
        queueHead = (queueHead + 1) % NOTIFICATION_QUEUE_SIZE;
        queueCount--;
    }

    // 新規通知をキューに追加
    notificationQueue[queueTail] = data;
    queueTail = (queueTail + 1) % NOTIFICATION_QUEUE_SIZE;
    queueCount++;

    consoleLog("[Notification] キューに追加: " + data.hypocenterName + " 震度" + data.maxIntensity + " (キュー内: " + String(queueCount) + "件)");

    // キュー処理を開始（現在通知中でなければ）
    processNotificationQueue();
}

/**
 * @brief 通知処理を更新（ノンブロッキング音声再生用）
 * @details loop()から毎回呼び出す。音声再生状態マシン、視覚通知、キュー処理を更新
 */
void updateNotification() {
    // ビープ音の状態マシン処理
    if (beepPlayed < beepCount) {
        unsigned long currentTime = millis();
        // 前回のビープから (BEEP_DURATION_MS + BEEP_INTERVAL_MS) 経過したか確認
        if (currentTime - lastBeepTime >= (BEEP_DURATION_MS + BEEP_INTERVAL_MS)) {
            beepPlayed++;
            if (beepPlayed < beepCount) {
                // 次のビープ音を再生
                if (isSpeakerEnabled) {
                    M5.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION_MS);
                }
                lastBeepTime = currentTime;
            } else {
                // すべてのビープ音再生完了
                consoleLog("[Notification] ビープ音再生完了");
                beepCount = 0;  // ビープ音状態をリセット
                // 次の通知をキューから処理
                processNotificationQueue();
            }
        }
    }

    // 視覚通知の更新
    updateFlashScreen();
}

/**
 * @brief 震度に応じたビープ回数を取得
 * @param intensity 震度文字列（"1", "2", "3", "4", "5弱", "5強", "6弱", "6強", "7"）
 * @return ビープ回数（1-3回）
 */
static int getBeepCountForIntensity(const String& intensity) {
    // 震度5弱以上: 3回
    if (intensity == "5弱" || intensity == "5強" ||
        intensity == "6弱" || intensity == "6強" ||
        intensity == "7") {
        return 3;
    }
    // 震度3-4: 2回
    else if (intensity == "3" || intensity == "4") {
        return 2;
    }
    // 震度1-2: 1回
    else {
        return 1;
    }
}

/**
 * @brief ビープ音を再生（状態マシン管理）
 * @param count ビープ回数（1-3回）
 * @details M5.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION_MS)でノンブロッキング再生
 *          updateNotification()で完了確認を行う
 */
static void playBeepSound(int count) {
    beepCount = count;
    beepPlayed = 0;
    lastBeepTime = millis();

    // 最初のビープ音を再生
    if (isSpeakerEnabled) {
        M5.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION_MS);
        consoleLog("[Notification] ビープ音再生開始: " + String(count) + "回");
    } else {
        consoleLog("[Notification] スピーカー無効、ビープ音スキップ");
    }
}

/**
 * @brief 通知キューから次の通知を処理
 * @details キューに通知がある場合、先頭から取り出して通知処理を開始
 */
static void processNotificationQueue() {
    // 現在通知処理中の場合はスキップ
    if (beepCount > 0) {
        return;
    }

    // キューが空の場合は何もしない
    if (queueCount == 0) {
        return;
    }

    // キューから先頭の通知を取り出し
    EarthquakeData data = notificationQueue[queueHead];
    queueHead = (queueHead + 1) % NOTIFICATION_QUEUE_SIZE;
    queueCount--;

    consoleLog("[Notification] 通知処理開始: " + data.hypocenterName + " 震度" + data.maxIntensity);

    // ビープ音再生
    int count = getBeepCountForIntensity(data.maxIntensity);
    playBeepSound(count);

    // 視覚通知（画面点滅）
    uint16_t color = getIntensityColor(data.maxIntensity);
    flashScreen(color);

    // リストに地震情報を追加
    addEarthquakeToDisplay(data);
}

/**
 * @brief 画面点滅を開始
 * @param color 点滅色（RGB565）
 * @details isFlashing=trueを設定し、updateFlashScreen()で点滅処理を実行
 */
static void flashScreen(uint16_t color) {
    isFlashing = true;
    flashStartTime = millis();
    flashColor = color;
    consoleLog("[Notification] 視覚通知開始（点滅色: 0x" + String(color, HEX) + "）");
}

/**
 * @brief 視覚通知の更新（タッチ検出、点滅アニメーション、自動終了）
 * @details updateNotification()から毎回呼び出される
 */
static void updateFlashScreen() {
    if (!isFlashing) {
        return;
    }

    unsigned long currentTime = millis();

    // タッチ検出（最優先、50ms以内の応答を保証）
    auto touch = M5.Touch.getDetail();
    if (touch.isPressed()) {
        // 即座に点滅を終了
        isFlashing = false;
        M5.Display.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH, VISIBLE_AREA_HEIGHT, COLOR_BG);  // メイン表示エリアのみクリア
        drawMainHeader();  // ヘッダを再描画
        renderList();  // リストを再描画
        consoleLog("[Notification] タッチ操作により通知を中断");
        return;
    }

    // 時間経過による点滅処理
    unsigned long elapsed = currentTime - flashStartTime;
    if (elapsed >= FLASH_DURATION_MS) {
        // 点滅終了
        isFlashing = false;
        M5.Display.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH, VISIBLE_AREA_HEIGHT, COLOR_BG);  // メイン表示エリアのみクリア
        drawMainHeader();  // ヘッダを再描画
        renderList();  // リストを再描画
        consoleLog("[Notification] 視覚通知完了");
        return;
    }

    // 点滅アニメーション（300ms間隔でON/OFF）
    bool shouldShowColor = ((elapsed / FLASH_INTERVAL_MS) % 2) == 0;
    if (shouldShowColor) {
        M5.Display.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH, VISIBLE_AREA_HEIGHT, flashColor);  // メイン表示エリアのみ点滅
    } else {
        M5.Display.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH, VISIBLE_AREA_HEIGHT, COLOR_BG);  // メイン表示エリアのみクリア
    }
}
