/**
 * @file websocket.cpp
 * @brief Symbol blockchain WebSocket接続と地震データ監視機能の実装
 */

#include "websocket.h"
#include "earthquake.h"
#include "notification.h"
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

using namespace websockets;

// 外部依存関数（main.cppで定義）
extern void consoleLog(String message);
extern bool isWiFiConnected;

// WebSocketクライアントオブジェクト
static WebsocketsClient webSocketClient;

// WebSocket接続URL（initWebSocket()で設定）
static String websocketUrl = "";

// サブスクリプション対象アドレス（initWebSocket()で設定）
static String subscriptionAddress = "";

// 署名者公開鍵（initWebSocket()で設定）
static String signerPubKey = "";

// WebSocket接続状態フラグ
static bool wsConnected = false;

// サーバーから受信したUID
static String serverUid = "";

// UID受信フラグ
static bool uidReceived = false;

// 再接続タイマー
static unsigned long reconnectTimer = 0;

// 連続失敗カウンター
static int consecutiveFailures = 0;

// 再接続間隔定数
static const unsigned long RECONNECT_INTERVAL = 5000;         // 5秒
static const unsigned long BACKOFF_INTERVAL = 60000;          // 1分
static const int MAX_CONSECUTIVE_FAILURES = 5;

// 重複検出用トランザクションハッシュバッファ（循環バッファ）
#define TX_HASH_BUFFER_SIZE 10
static String txHashBuffer[TX_HASH_BUFFER_SIZE];
static int txHashIndex = 0;

// メモリ監視用定数とタイマー
static unsigned long memoryCheckTimer = 0;
static const unsigned long MEMORY_CHECK_INTERVAL = 10000;     // 10秒
static const unsigned long LOW_MEMORY_THRESHOLD = 20000;      // 20KB
static const unsigned long CRITICAL_MEMORY_THRESHOLD = 15000; // 15KB

// Ping/Pong監視用タイマー
static unsigned long lastPingSentTime = 0;      // 最後にPingを送信した時刻（0は未送信）
static unsigned long lastPongReceivedTime = 0;  // 最後にPongを受信した時刻

// Ping/Pong監視用定数
static const unsigned long PING_INTERVAL = 60000;   // Ping送信間隔（60秒）
static const unsigned long PONG_TIMEOUT = 30000;    // Pong応答タイムアウト（30秒）

// 前方宣言
static bool connectWebSocket();
static void disconnectWebSocket();
static void subscribeToTransactions(const String &uid);
static void handleWebSocketMessage(const String &message);
static bool isDuplicateTransaction(const String &txHash);
static void addTransactionHash(const String &txHash);
static void monitorMemory();
static void onWebSocketConnect();
static void onWebSocketDisconnect();
static void onWebSocketError(String error);
static void sendPing();
static bool checkPongTimeout();

/**
 * @brief トランザクションハッシュの重複チェック
 * @param txHash トランザクションハッシュ（64文字16進数文字列）
 * @return 重複している場合true、新規の場合false
 */
static bool isDuplicateTransaction(const String &txHash) {
    for (int i = 0; i < TX_HASH_BUFFER_SIZE; i++) {
        if (txHashBuffer[i] == txHash) {
            return true;  // 重複検出
        }
    }
    return false;  // 新規トランザクション
}

/**
 * @brief 重複検出バッファにトランザクションハッシュを追加
 * @param txHash トランザクションハッシュ（64文字16進数文字列）
 */
static void addTransactionHash(const String &txHash) {
    txHashBuffer[txHashIndex] = txHash;
    txHashIndex = (txHashIndex + 1) % TX_HASH_BUFFER_SIZE;  // 循環バッファ
}

/**
 * @brief Ping送信処理
 * @details WebSocket接続中にPingフレームを送信し、送信時刻を記録
 */
static void sendPing() {
    if (wsConnected) {
        bool success = webSocketClient.ping();
        if (success) {
            lastPingSentTime = millis();
            consoleLog("[WebSocket] Ping送信");
        } else {
            consoleLog("[WebSocket] Ping送信失敗");
            // Ping送信失敗時はlastPingSentTimeを更新しない（次回再試行）
        }
    }
}

/**
 * @brief Pong応答タイムアウトチェック
 * @return タイムアウト検出時true、正常時false
 * @details Ping送信後30秒以内にPong応答がない場合、接続断と判断
 * @note millis()オーバーフロー対策: unsigned long型の減算により、
 *       オーバーフロー時も正しく動作（49.7日ごとのオーバーフロー）
 */
static bool checkPongTimeout() {
    // Ping未送信時はチェックしない
    if (lastPingSentTime == 0) {
        return false;
    }

    // Pong受信済みの場合はタイムアウトしない
    if (lastPongReceivedTime >= lastPingSentTime) {
        return false;
    }

    unsigned long currentTime = millis();
    unsigned long elapsedSincePing = currentTime - lastPingSentTime;

    // Pongタイムアウトチェック（30秒超過）
    if (elapsedSincePing > PONG_TIMEOUT) {
        consoleLog("[WebSocket] Pong応答タイムアウト、接続断と判断");
        disconnectWebSocket();

        // 再接続タイマー開始（consecutiveFailuresは増加させない）
        reconnectTimer = currentTime;

        return true;
    }

    return false;
}

/**
 * @brief メモリ使用量を監視し、必要に応じて警告または切断
 */
static void monitorMemory() {
    unsigned long currentTime = millis();

    if (currentTime - memoryCheckTimer >= MEMORY_CHECK_INTERVAL) {
        memoryCheckTimer = currentTime;

        uint32_t freeHeap = ESP.getFreeHeap();

        if (freeHeap < CRITICAL_MEMORY_THRESHOLD) {
            consoleLog("[WebSocket] メモリ不足、切断: Free heap = " + String(freeHeap) + " bytes");
            disconnectWebSocket();
            // 1分待機してから再接続試行
            reconnectTimer = currentTime;
            consecutiveFailures = MAX_CONSECUTIVE_FAILURES;  // バックオフ間隔を使用
        } else if (freeHeap < LOW_MEMORY_THRESHOLD) {
            consoleLog("[WebSocket] メモリ警告: Free heap = " + String(freeHeap) + " bytes");
        }
    }
}

/**
 * @brief Symbol blockchain WebSocketにサブスクリプションを送信
 * @details 該当アドレス宛てのconfirmed transactionを監視
 * @param uid サーバーから受信したUID
 */
static void subscribeToTransactions(const String &uid) {
    String subscription = "{\"uid\":\"" + uid + "\",\"subscribe\":\"confirmedAdded/" + subscriptionAddress + "\"}";

    consoleLog("[WebSocket] サブスクリプション送信: " + subscription);
    webSocketClient.send(subscription);
}

/**
 * @brief WebSocketメッセージ受信時のコールバック
 * @param message 受信したメッセージ（JSON文字列）
 */
static void handleWebSocketMessage(const String &message) {
    consoleLog("[WebSocket] メッセージ受信 (長さ: " + String(message.length()) + ")");
    consoleLog("[WebSocket] 内容: " + message);

    // JSON解析
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        consoleLog("[WebSocket] JSON解析エラー: " + String(error.c_str()));
        return;
    }

    // UID受信チェック（接続直後にサーバーから送られる）
    if (doc["uid"].is<const char*>() && !doc["data"].is<JsonObject>() && !doc["topic"].is<const char*>()) {
        serverUid = doc["uid"].as<String>();
        uidReceived = true;
        consoleLog("[WebSocket] サーバーからUIDを受信: " + serverUid);

        // UIDを受信したのでサブスクリプションを送信
        subscribeToTransactions(serverUid);
        return;
    }

    // トランザクション情報の取得
    if (!doc["data"].is<JsonObject>()) {
        // dataフィールドがない場合はスキップ（サブスクリプション応答など）
        consoleLog("[WebSocket] サブスクリプション確認応答を受信、接続維持");
        return;
    }

    JsonObject data = doc["data"];

    if (!data["transaction"].is<JsonObject>()) {
        consoleLog("[WebSocket] transactionキーなし");
        return;
    }

    JsonObject transaction = data["transaction"];

    // 署名者公開鍵を検証（設定されている場合のみ）
    String signerPublicKey = transaction["signerPublicKey"] | "";

    if (signerPubKey.length() > 0 && signerPublicKey != signerPubKey) {
        // 署名者が一致しない場合は無視（ログ出力なし、ノイズ削減）
        return;
    }

    // メッセージフィールドの抽出
    String hexMessage = transaction["message"] | "";

    if (hexMessage.length() == 0) {
        consoleLog("[WebSocket] messageフィールドが空");
        return;
    }

    // メタ情報からトランザクションハッシュを取得
    String txHash = data["meta"]["hash"] | "";

    // 重複検出
    if (isDuplicateTransaction(txHash)) {
        consoleLog("[WebSocket] 重複トランザクションをスキップ: " + txHash.substring(0, 16) + "...");
        return;
    }

    // 新規トランザクションとしてハッシュを記録
    addTransactionHash(txHash);

    // 地震情報をパース
    EarthquakeData earthquakeData;
    if (!parseWebSocketMessage(hexMessage, earthquakeData)) {
        // エラーはparseWebSocketMessage内でログ出力済み
        return;
    }

    // 地震情報をログ出力
    consoleLog("[WebSocket] 新しい地震情報を検出");
    consoleLog("発生時刻: " + earthquakeData.datetime);
    consoleLog("震源地: " + earthquakeData.hypocenterName);
    consoleLog("マグニチュード: M" + String(earthquakeData.magnitude, 1));
    consoleLog("最大震度: " + earthquakeData.maxIntensity);
    consoleLog("深さ: " + String(earthquakeData.depth) + "km");
    consoleLog("津波: " + earthquakeData.tsunami);

    // 通知機能を呼び出す（重複検出済みと想定）
    notifyEarthquake(earthquakeData);
}

/**
 * @brief WebSocket接続確立時のコールバック
 */
static void onWebSocketConnect() {
    consoleLog("[WebSocket] 接続成功、サーバーからのUID待機中...");
    wsConnected = true;
    consecutiveFailures = 0;  // 連続失敗カウンターリセット
    uidReceived = false;      // UID受信フラグリセット
    serverUid = "";           // UIDクリア

    // Ping/Pong監視の初期化
    lastPingSentTime = 0;            // Ping未送信状態
    lastPongReceivedTime = millis(); // 接続確立時刻を記録

    // サブスクリプションはUID受信後に送信（handleWebSocketMessageで処理）
}

/**
 * @brief WebSocket切断時のコールバック
 */
static void onWebSocketDisconnect() {
    consoleLog("[WebSocket] 切断（サーバーまたはネットワークにより切断されました）");
    wsConnected = false;
    uidReceived = false;  // UID受信フラグリセット
    serverUid = "";       // UIDクリア
}

/**
 * @brief WebSocketエラー時のコールバック
 * @param error エラーメッセージ
 */
static void onWebSocketError(String error) {
    consoleLog("[WebSocket] エラー: " + error);
}

/**
 * @brief WebSocketサーバーに接続
 * @return 接続成功時true、失敗時false
 */
static bool connectWebSocket() {
    consoleLog("[WebSocket] 接続試行: " + websocketUrl);

    // WebSocket接続（非暗号化、開発環境用）
    // イベントハンドラーはinitWebSocket()で既に登録済み
    bool connected = webSocketClient.connect(websocketUrl);

    if (!connected) {
        consoleLog("[WebSocket] 接続失敗");
        consecutiveFailures++;
        return false;
    }

    return true;
}

/**
 * @brief WebSocket切断処理
 */
static void disconnectWebSocket() {
    if (wsConnected) {
        consoleLog("[WebSocket] 切断処理");
        webSocketClient.close();
        wsConnected = false;
    }
}

/**
 * @brief WebSocket機能を初期化
 * @param config Symbol設定（network, node, address, pubKey）
 * @details WebSocketクライアントのセットアップ、イベントハンドラー登録、重複検出バッファ初期化
 */
void initWebSocket(const SymbolConfig &config) {
    // 状態変数の初期化
    wsConnected = false;
    uidReceived = false;
    serverUid = "";
    reconnectTimer = 0;
    consecutiveFailures = 0;

    // WebSocket URL生成（node URLから変換）
    // 例: "https://sym-test-03.opening-line.jp:3001" -> "ws://sym-test-03.opening-line.jp:3000/ws"
    websocketUrl = config.node;
    websocketUrl.replace("https://", "ws://");
    websocketUrl.replace(":3001", ":3000");  // REST APIポート -> WebSocketポート
    if (!websocketUrl.endsWith("/ws")) {
        // パスが含まれている場合は除去
        int pathStart = websocketUrl.indexOf('/', 5);  // "ws://"の後の最初の'/'
        if (pathStart > 0) {
            websocketUrl = websocketUrl.substring(0, pathStart);
        }
        websocketUrl += "/ws";
    }
    consoleLog("[WebSocket] URL設定: " + websocketUrl);

    // サブスクリプション対象アドレス設定
    subscriptionAddress = config.address;
    consoleLog("[WebSocket] 監視アドレス: " + subscriptionAddress);

    // 署名者公開鍵設定
    signerPubKey = config.pubKey;
    if (signerPubKey.length() > 0) {
        consoleLog("[WebSocket] 公開鍵フィルター有効: " + signerPubKey.substring(0, min(16, (int)signerPubKey.length())) + "...");
    } else {
        consoleLog("[WebSocket] 公開鍵フィルター無効（すべてのトランザクションを受信）");
    }

    // 重複検出バッファの初期化（空文字列で初期化）
    for (int i = 0; i < TX_HASH_BUFFER_SIZE; i++) {
        txHashBuffer[i] = "";
    }
    txHashIndex = 0;

    // イベントハンドラー登録（1回のみ、ここで登録）
    webSocketClient.onMessage([](WebsocketsMessage message) {
        handleWebSocketMessage(message.data());
    });
    webSocketClient.onEvent([](WebsocketsEvent event, String data) {
        if (event == WebsocketsEvent::ConnectionOpened) {
            onWebSocketConnect();
        } else if (event == WebsocketsEvent::ConnectionClosed) {
            onWebSocketDisconnect();
        } else if (event == WebsocketsEvent::GotPing) {
            consoleLog("[WebSocket] Ping受信");
        } else if (event == WebsocketsEvent::GotPong) {
            consoleLog("[WebSocket] Pong受信、接続正常");
            lastPongReceivedTime = millis();
        }
    });

    consoleLog("[WebSocket] 初期化完了");
}

/**
 * @brief WebSocketループ処理（loop()から呼び出し）
 * @details 接続状態確認、メッセージ受信、再接続処理、メモリ監視
 */
void webSocketLoop() {
    // WiFi接続チェック
    if (!isWiFiConnected) {
        // WiFi切断時はWebSocketも切断し、連続失敗カウンターをリセット
        if (wsConnected) {
            disconnectWebSocket();
            consecutiveFailures = 0;
        }
        return;
    }

    // WebSocket接続状態確認
    if (!wsConnected) {
        // 再接続タイマーチェック
        unsigned long currentTime = millis();
        unsigned long interval = (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) ? BACKOFF_INTERVAL : RECONNECT_INTERVAL;

        if (currentTime - reconnectTimer >= interval) {
            // 5回連続失敗後の1分待機をログ出力
            if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES && currentTime - reconnectTimer >= BACKOFF_INTERVAL) {
                consoleLog("[WebSocket] 5回連続失敗、1分間待機後に再接続試行");
            }

            // 再接続試行
            if (connectWebSocket()) {
                // 接続成功時はonWebSocketConnect()でconsecutiveFailuresがリセットされる
            } else {
                // 接続失敗時は再接続タイマーを更新
                reconnectTimer = currentTime;
            }
        }
    } else {
        // WebSocket接続中の場合、メッセージをポーリング（常に呼び出す）
        webSocketClient.poll();

        // Ping/Pong監視
        // Ping送信タイミングチェック（60秒経過）
        if (millis() - lastPingSentTime >= PING_INTERVAL) {
            sendPing();
        }

        // Pongタイムアウトチェック
        checkPongTimeout();
    }

    // メモリ監視（毎回実行）
    monitorMemory();
}

/**
 * @brief WebSocket接続状態を取得
 * @return 接続中ならtrue、切断中ならfalse
 */
bool getWebSocketConnected() {
    return wsConnected;
}
