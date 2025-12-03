/**
 * @file network.h
 * @brief WiFi接続およびNTP時刻同期の処理
 * @details WiFi設定の読み込み、WiFi接続、NTP時刻同期を行う関数群
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

// main.cppで定義されているconsoleLog関数の宣言
extern void consoleLog(String message);

// SDカード設定
#define TFCARD_CS_PIN 4  // SDカードのCSピン

// WiFi設定（SDカード読み込み失敗時のフォールバック）
#define WIFI_SSID "xxxxxxxxx"
#define WIFI_PASSWORD "xxxxxxxxx"
#define WIFI_CONNECT_TIMEOUT 10000  // WiFi接続タイムアウト（ミリ秒）

// NTP設定
#define NTP_SERVER "ntp.nict.jp"           // 日本の公式NTPサーバー（NICT）
#define NTP_TIMEZONE_JST 9 * 3600          // 日本標準時（UTC+9）
#define NTP_SYNC_TIMEOUT 20000             // 20秒に延長

// タイムゾーン設定
#define CONFIG_TIMEZONE_FILE_PATH "/config.ini"
#define DEFAULT_TIMEZONE_NAME "Asia/Tokyo"
#define DEFAULT_TIMEZONE_OFFSET (9 * 3600)  // JST (UTC+9) = 32400秒

// Symbol設定（SDカード読み込み失敗時のフォールバック）
#define SYMBOL_DEFAULT_NETWORK "mainnet"
#define SYMBOL_DEFAULT_NODE "https://dual-1.nodes-xym.work:3001"
#define SYMBOL_DEFAULT_ADDRESS "NADMA4NNPH2E2XMFGJNTKFYJARRH5VTKXAPUJNQ"
#define SYMBOL_DEFAULT_PUBKEY "B1A216D31CF6A1F10F393064DD1A447F02AE327FC27359DDC32B07B56021326E"

// Symbolバリデーション定数
#define SYMBOL_ADDRESS_LENGTH 39
#define SYMBOL_PUBKEY_LENGTH 64
#define SYMBOL_NODE_MAX_LENGTH 200

// SD WiFi設定ファイル
#define CONFIG_FILE_PATH "/wifi.ini"
#define CONFIG_FILE_MAX_SIZE 4096
#define SSID_MAX_LENGTH 32
#define PASSWORD_MAX_LENGTH 63

/**
 * @brief Symbol設定データ構造
 */
struct SymbolConfig {
    String network;    // ネットワーク種別（"testnet" or "mainnet"）
    String node;       // ノードURL（"https://..."で始まる、最大200文字）
    String address;    // Symbolアドレス（39文字、先頭N/T）
    String pubKey;     // 公開鍵（64文字の16進数）
};

// WiFi関連関数の宣言

/**
 * @brief SDカードからWiFi設定を読み込み
 * @param ssid WiFi SSID（出力パラメータ）
 * @param password WiFiパスワード（出力パラメータ）
 * @return 成功時true、失敗時false
 */
bool loadWiFiConfigFromSD(String &ssid, String &password);

/**
 * @brief WiFi認証情報を取得（SD優先、フォールバックはハードコード値）
 * @param ssid WiFi SSID（出力パラメータ）
 * @param password WiFiパスワード（出力パラメータ）
 */
void getWiFiCredentials(String &ssid, String &password);

/**
 * @brief WiFiに接続
 * @param ssid WiFi SSID
 * @param password WiFiパスワード
 * @return 接続成功時true、失敗時false
 */
bool connectToWiFi(const String &ssid, const String &password);

/**
 * @brief NTP時刻同期を実行
 * @param timezoneOffset タイムゾーンoffset（秒単位）
 * @return 同期成功時true、失敗時false
 */
bool syncNTP(int32_t timezoneOffset);

/**
 * @brief タイムゾーン設定を取得（SD優先、フォールバックはデフォルト値）
 * @return タイムゾーンoffset（秒単位）
 */
int32_t getTimezoneConfig();

/**
 * @brief SDカードからSymbol設定を読み込み
 * @param config Symbol設定構造体（出力パラメータ）
 * @return 成功時true、失敗時false
 */
bool loadSymbolConfigFromSD(SymbolConfig &config);

/**
 * @brief Symbol設定を取得（SD優先、フォールバックはハードコード値）
 * @return Symbol設定構造体
 */
SymbolConfig getSymbolConfig();

#endif // NETWORK_H
