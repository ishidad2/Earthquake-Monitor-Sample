/**
 * @file earthquake.cpp
 * @brief Symbol blockchainから地震情報を取得する処理の実装
 */

#include "earthquake.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi接続状態（main.cppで定義）
extern bool isWiFiConnected;

// グローバルデータバッファ定義（display.cppとの連携用、Phase 1暫定実装）
EarthquakeData earthquakeDataBuffer[10];
int earthquakeDataBufferCount = 0;

// 前方宣言
static String decodeHexMessage(const String &hexMessage);
static bool parseEarthquakeJson(const String &earthquakeJson, EarthquakeData &data);
static void printEarthquakeData(const EarthquakeData &data, int index, int total);

/**
 * @brief トランザクション配列から地震情報を抽出
 * @param jsonResponse API応答JSON文字列
 * @param signerPubKey 検証する署名者公開鍵
 * @param count 処理する件数
 * @return 処理成功件数
 */
static int parseTransactions(const String &jsonResponse, const String &signerPubKey, int count) {
    consoleLog("API応答サイズ: " + String(jsonResponse.length()) + " bytes");

    JsonDocument doc;  // トランザクション配列用
    DeserializationError error = deserializeJson(doc, jsonResponse);

    if (error) {
        consoleLog("JSON解析失敗: " + String(error.c_str()));
        return 0;
    }

    // dataフィールドからトランザクション配列を取得
    JsonArray transactions = doc["data"].as<JsonArray>();
    consoleLog("トランザクション数: " + String(transactions.size()));

    int successCount = 0;
    int skipCount = 0;
    int errorCount = 0;

    int index = 0;
    for (JsonObject tx : transactions) {
        index++;
        if (successCount >= count) {
            break;
        }

        consoleLog("--- トランザクション " + String(index) + " ---");

        // トランザクションオブジェクトの確認
        if (!tx["transaction"].is<JsonObject>()) {
            consoleLog("transactionキーなし");
            errorCount++;
            continue;
        }

        JsonObject transaction = tx["transaction"];

        // 署名者公開鍵を検証
        String signerPublicKey = transaction["signerPublicKey"] | "";
        consoleLog("signerPublicKey: " + signerPublicKey.substring(0, min(16, (int)signerPublicKey.length())) + "...");

        if (signerPubKey.length() > 0 && signerPublicKey != signerPubKey) {
            consoleLog("無効なトランザクションをスキップ");
            skipCount++;
            continue;
        }

        // メッセージフィールドの存在確認と抽出
        String hexMessage = "";

        // messageがオブジェクトか文字列かを確認
        if (transaction["message"].is<JsonObject>()) {
            JsonObject messageObj = transaction["message"];
            hexMessage = messageObj["payload"] | "";

            if (hexMessage.length() == 0) {
                consoleLog("payloadが空");
                errorCount++;
                continue;
            }
        } else if (transaction["message"].is<const char*>()) {
            hexMessage = transaction["message"] | "";

            if (hexMessage.length() == 0) {
                consoleLog("messageが空");
                errorCount++;
                continue;
            }
        } else {
            consoleLog("messageが不明な型");
            errorCount++;
            continue;
        }

        // 16進数メッセージをデコード
        String earthquakeJson = decodeHexMessage(hexMessage);
        if (earthquakeJson.length() == 0) {
            errorCount++;
            continue;
        }

        // 地震情報JSONをパース
        EarthquakeData data;
        if (parseEarthquakeJson(earthquakeJson, data)) {
            printEarthquakeData(data, successCount + 1, count);

            // グローバルバッファに格納（display.cpp連携用、Phase 1暫定実装）
            if (successCount < 10) {
                earthquakeDataBuffer[successCount] = data;
                earthquakeDataBufferCount = successCount + 1;
            }

            successCount++;
        } else {
            errorCount++;
        }
    }

    // 処理サマリーログ出力
    consoleLog("処理サマリー: 成功=" + String(successCount) + "件, スキップ=" + String(skipCount) + "件, エラー=" + String(errorCount) + "件");

    return successCount;
}

/**
 * @brief 16進数文字列をUTF-8文字列にデコード
 * @param hexMessage 16進数文字列
 * @return デコードされたUTF-8文字列
 */
static String decodeHexMessage(const String &hexMessage) {
    // 奇数長チェック
    if (hexMessage.length() % 2 != 0) {
        consoleLog("16進数デコード失敗: 奇数長");
        return "";
    }

    String result = "";
    // Symbol blockchainメッセージは最初の1バイト(00)をスキップ
    for (size_t i = 2; i < hexMessage.length(); i += 2) {
        // 2文字を16進数として解釈
        String byteString = hexMessage.substring(i, i + 2);
        char *endPtr;
        long byteValue = strtol(byteString.c_str(), &endPtr, 16);

        // 不正な16進数文字チェック
        if (*endPtr != '\0') {
            consoleLog("16進数デコード失敗: 不正な16進数文字 at position " + String(i) + ": " + byteString);
            return "";
        }

        result += (char)byteValue;
    }

    return result;
}

/**
 * @brief 地震情報JSONをパースしてデータ構造体に変換
 * @param earthquakeJson 地震情報JSON文字列
 * @param data 出力用EarthquakeData構造体
 * @return パース成功時true、失敗時false
 */
static bool parseEarthquakeJson(const String &earthquakeJson, EarthquakeData &data) {
    JsonDocument doc;  // 地震情報1件用
    DeserializationError error = deserializeJson(doc, earthquakeJson);

    if (error) {
        consoleLog("地震情報JSON解析失敗: " + String(error.c_str()));
        return false;
    }

    // "earthquake"キーの存在確認
    if (!doc["earthquake"].is<JsonObject>()) {
        consoleLog("earthquakeキーが見つかりません");
        return false;
    }

    JsonObject eq = doc["earthquake"];

    // 必須フィールド存在確認
    if (!eq["time"].is<String>() || !eq["hypocenter"].is<JsonObject>() || eq["maxScale"].isNull()) {
        consoleLog("必須フィールドが欠損しています");
        return false;
    }

    // フィールドを格納
    data.datetime = eq["time"] | "";
    data.hypocenterName = eq["hypocenter"]["name"] | "";
    data.latitude = eq["hypocenter"]["latitude"] | 0.0f;
    data.longitude = eq["hypocenter"]["longitude"] | 0.0f;
    data.depth = eq["hypocenter"]["depth"] | 0;
    data.magnitude = eq["hypocenter"]["magnitude"] | 0.0f;

    // maxScaleを震度文字列に変換（10=震度1, 20=震度2, ...）
    int maxScale = eq["maxScale"] | 0;
    if (maxScale >= 10 && maxScale <= 40) {
        data.maxIntensity = String(maxScale / 10);
    } else if (maxScale == 45) {
        data.maxIntensity = "5弱";
    } else if (maxScale == 50) {
        data.maxIntensity = "5強";
    } else if (maxScale == 55) {
        data.maxIntensity = "6弱";
    } else if (maxScale == 60) {
        data.maxIntensity = "6強";
    } else if (maxScale == 70) {
        data.maxIntensity = "7";
    } else {
        // 震度が不明な場合は無効なデータとして扱う
        consoleLog("震度が不明なため、このデータをスキップします");
        return false;
    }

    data.tsunami = eq["domesticTsunami"] | "";

    // 不完全なデータをフィルタリング
    if (data.hypocenterName.length() == 0) {
        consoleLog("震源地が空のため、このデータをスキップします");
        return false;
    }
    if (data.magnitude < 0) {
        consoleLog("マグニチュードが無効なため、このデータをスキップします");
        return false;
    }

    return true;
}

/**
 * @brief 地震情報をシリアルコンソールに出力
 * @param data 地震情報データ
 * @param index 現在の処理番号
 * @param total 総処理件数
 */
static void printEarthquakeData(const EarthquakeData &data, int index, int total) {
    consoleLog("[地震情報 " + String(index) + "/" + String(total) + "]");
    consoleLog("発生時刻: " + data.datetime);
    consoleLog("震源地: " + data.hypocenterName);
    consoleLog("マグニチュード: M" + String(data.magnitude, 1));
    consoleLog("最大震度: " + data.maxIntensity);
    consoleLog("深さ: " + String(data.depth) + "km");
    consoleLog("津波: " + data.tsunami);
    consoleLog("---");
}

/**
 * @brief Symbol blockchain APIにHTTPSリクエストを送信
 * @param url リクエストURL
 * @param response レスポンス文字列（出力パラメータ）
 * @return HTTP成功時true、失敗時false
 */
static bool sendHttpsRequest(const String &url, String &response) {
    WiFiClientSecure client;
    HTTPClient http;

    // 開発環境: TLS証明書検証をスキップ
    client.setInsecure();

    // HTTPClient初期化
    http.setTimeout(HTTP_READ_TIMEOUT);

    if (!http.begin(client, url)) {
        consoleLog("HTTP begin failed");
        return false;
    }

    // GETリクエスト送信
    int httpCode = http.GET();
    consoleLog("HTTP Status: " + String(httpCode));

    if (httpCode != HTTP_CODE_OK) {
        consoleLog("HTTP Error: " + String(httpCode));
        http.end();
        return false;
    }

    // レスポンス本文取得
    response = http.getString();
    http.end();

    // 応答の最初の200文字をログ出力（デバッグ用）
    if (response.length() > 0) {
        String preview = response.substring(0, min(200, (int)response.length()));
        consoleLog("応答プレビュー: " + preview);
    }

    return true;
}

/**
 * @brief 地震情報を取得してシリアルコンソールに出力
 * @param config Symbol設定（network, node, address, pubKey）
 * @param count 取得する地震情報の件数
 * @return 取得成功時true、失敗時false
 */
bool fetchEarthquakeData(const SymbolConfig &config, int count) {
    // WiFi接続チェック
    if (!isWiFiConnected) {
        consoleLog("WiFi not connected. Skipping earthquake data fetch.");
        return false;
    }

    consoleLog("地震情報取得開始");

    // API URL構築（SDから読み込んだ設定を使用）
    String url = config.node + "/transactions/confirmed" +
                 "?address=" + config.address +
                 "&pageSize=" + String(count) +
                 "&order=desc";

    consoleLog("Request URL: " + url);

    // HTTPSリクエスト送信
    String response;
    if (!sendHttpsRequest(url, response)) {
        consoleLog("API接続失敗");
        return false;
    }

    // トランザクション配列をパース
    int successCount = parseTransactions(response, config.pubKey, count);

    consoleLog("地震情報取得完了: " + String(successCount) + "件");
    return successCount > 0;
}

/**
 * @brief WebSocketメッセージ（16進数）を地震情報にパース
 * @details decodeHexMessage()とparseEarthquakeJson()を組み合わせたラッパー関数
 * @param hexMessage 16進数文字列（Symbol blockchainトランザクションメッセージ）
 * @param data 出力用EarthquakeData構造体
 * @return パース成功時true、失敗時false
 */
bool parseWebSocketMessage(const String &hexMessage, EarthquakeData &data) {
    // 16進数デコード（内部static関数を呼び出し）
    String earthquakeJson = decodeHexMessage(hexMessage);
    if (earthquakeJson.length() == 0) {
        consoleLog("[WebSocket] 16進数デコード失敗");
        return false;
    }

    // JSON解析（内部static関数を呼び出し）
    if (!parseEarthquakeJson(earthquakeJson, data)) {
        consoleLog("[WebSocket] JSON解析失敗");
        return false;
    }

    return true;
}
