/**
 * @file earthquake.h
 * @brief Symbol blockchainから地震情報を取得する処理
 * @details REST APIを使用してトランザクションから地震情報を取得・パース
 */

#ifndef EARTHQUAKE_H
#define EARTHQUAKE_H

#include <Arduino.h>
#include "network.h"  // SymbolConfig構造体を使用

// main.cppで定義されているconsoleLog関数の宣言
extern void consoleLog(String message);

// タイムアウト設定
#define HTTP_CONNECT_TIMEOUT 10000  // HTTP接続タイムアウト（ミリ秒）
#define HTTP_READ_TIMEOUT 10000     // HTTP読み取りタイムアウト（ミリ秒）

/**
 * @brief 地震情報データ構造体
 * @details Symbol blockchainから取得した地震情報を格納
 */
struct EarthquakeData {
    String datetime;           // 発生時刻（例: "2024-01-15T14:30:00+09:00"）
    String hypocenterName;     // 震源地名（例: "茨城県沖"）
    float latitude;            // 緯度（度）
    float longitude;           // 経度（度）
    int depth;                 // 深さ（km）
    float magnitude;           // マグニチュード
    String maxIntensity;       // 最大震度（例: "5弱", "4", "7"）
    String tsunami;            // 津波警報状態（例: "なし", "注意報", "警報"）
};

// グローバルデータバッファ（display.cppとの連携用、Phase 1暫定実装）
// 注: Phase 2でコールバック方式にリファクタリング予定
extern EarthquakeData earthquakeDataBuffer[10];
extern int earthquakeDataBufferCount;

/**
 * @brief 地震情報を取得してシリアルコンソールに出力
 * @param config Symbol設定（network, node, address, pubKey）
 * @param count 取得する地震情報の件数（デフォルト: 10）
 * @return 取得成功時true、失敗時false
 */
bool fetchEarthquakeData(const SymbolConfig &config, int count = 10);

/**
 * @brief WebSocketメッセージ（16進数）を地震情報にパース
 * @details decodeHexMessage()とparseEarthquakeJson()を組み合わせたラッパー関数
 * @param hexMessage 16進数文字列（Symbol blockchainトランザクションメッセージ）
 * @param data 出力用EarthquakeData構造体
 * @return パース成功時true、失敗時false
 */
bool parseWebSocketMessage(const String &hexMessage, EarthquakeData &data);

#endif // EARTHQUAKE_H
