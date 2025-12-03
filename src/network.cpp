/**
 * @file network.cpp
 * @brief WiFi接続およびNTP時刻同期の実装
 */

#include "network.h"
#include <WiFi.h>
#include <SD.h>
#include <time.h>

// タイムゾーンマッピング構造体
struct TimezoneMapping {
    const char* name;        // タイムゾーン名（最大30文字）
    int32_t offsetSeconds;   // UTC offset（秒単位）
};

// タイムゾーン名をPROGMEMに配置
const char tz_tokyo[] PROGMEM = "Asia/Tokyo";
const char tz_new_york[] PROGMEM = "America/New_York";
const char tz_shanghai[] PROGMEM = "Asia/Shanghai";
const char tz_singapore[] PROGMEM = "Asia/Singapore";
const char tz_hong_kong[] PROGMEM = "Asia/Hong_Kong";
const char tz_seoul[] PROGMEM = "Asia/Seoul";
const char tz_bangkok[] PROGMEM = "Asia/Bangkok";
const char tz_dubai[] PROGMEM = "Asia/Dubai";
const char tz_kolkata[] PROGMEM = "Asia/Kolkata";
const char tz_chicago[] PROGMEM = "America/Chicago";
const char tz_denver[] PROGMEM = "America/Denver";
const char tz_los_angeles[] PROGMEM = "America/Los_Angeles";
const char tz_sao_paulo[] PROGMEM = "America/Sao_Paulo";
const char tz_london[] PROGMEM = "Europe/London";
const char tz_paris[] PROGMEM = "Europe/Paris";
const char tz_berlin[] PROGMEM = "Europe/Berlin";
const char tz_moscow[] PROGMEM = "Europe/Moscow";
const char tz_auckland[] PROGMEM = "Pacific/Auckland";
const char tz_sydney[] PROGMEM = "Australia/Sydney";
const char tz_utc[] PROGMEM = "UTC";
const char tz_gmt[] PROGMEM = "GMT";

// タイムゾーンマッピングテーブル
static const TimezoneMapping timezoneTable[] PROGMEM = {
    {tz_tokyo, 9 * 3600},           // UTC+9
    {tz_new_york, -5 * 3600},       // UTC-5
    {tz_shanghai, 8 * 3600},        // UTC+8
    {tz_singapore, 8 * 3600},       // UTC+8
    {tz_hong_kong, 8 * 3600},       // UTC+8
    {tz_seoul, 9 * 3600},           // UTC+9
    {tz_bangkok, 7 * 3600},         // UTC+7
    {tz_dubai, 4 * 3600},           // UTC+4
    {tz_kolkata, 19800},            // UTC+5.5 (5.5 * 3600)
    {tz_chicago, -6 * 3600},        // UTC-6
    {tz_denver, -7 * 3600},         // UTC-7
    {tz_los_angeles, -8 * 3600},    // UTC-8
    {tz_sao_paulo, -3 * 3600},      // UTC-3
    {tz_london, 0},                 // UTC+0
    {tz_paris, 1 * 3600},           // UTC+1
    {tz_berlin, 1 * 3600},          // UTC+1
    {tz_moscow, 3 * 3600},          // UTC+3
    {tz_auckland, 12 * 3600},       // UTC+12
    {tz_sydney, 10 * 3600},         // UTC+10
    {tz_utc, 0},                    // UTC+0
    {tz_gmt, 0}                     // UTC+0
};

const int timezoneTableSize = sizeof(timezoneTable) / sizeof(TimezoneMapping);

bool loadWiFiConfigFromSD(String &ssid, String &password) {
    consoleLog("Mounting SD card...");

    // SDカードマウント（M5Stack Core用のピン設定）
    // TFCARD_CS_PIN = 4 (M5Stack.hで定義)
    // クロック速度を下げて安定性向上（40MHz → 4MHz）
    if (!SD.begin(TFCARD_CS_PIN, SPI, 4000000)) {
        consoleLog("SD card mount failed.");
        return false;
    }
    consoleLog("SD card mounted successfully");

    // ファイル存在確認
    if (!SD.exists(CONFIG_FILE_PATH)) {
        consoleLog("wifi.ini not found.");
        return false;
    }

    // ファイルオープン
    File configFile = SD.open(CONFIG_FILE_PATH, FILE_READ);
    if (!configFile) {
        consoleLog("Failed to open wifi.ini.");
        return false;
    }

    // ファイルサイズチェック
    if (configFile.size() > CONFIG_FILE_MAX_SIZE) {
        consoleLog("wifi.ini file too large.");
        configFile.close();
        return false;
    }

    // SSID読み込み（1行目）
    if (!configFile.available()) {
        consoleLog("wifi.ini format error: no SSID line.");
        configFile.close();
        return false;
    }
    ssid = configFile.readStringUntil('\n');
    ssid.trim();  // CRLF対応

    // パスワード読み込み（2行目）
    if (!configFile.available()) {
        consoleLog("wifi.ini format error: no password line.");
        configFile.close();
        return false;
    }
    password = configFile.readStringUntil('\n');
    password.trim();  // CRLF対応

    configFile.close();

    // 空文字列チェック
    if (ssid.length() == 0 || password.length() == 0) {
        consoleLog("Invalid wifi.ini: SSID or password is empty.");
        return false;
    }

    // SSID文字数チェック
    if (ssid.length() > SSID_MAX_LENGTH) {
        consoleLog("Warning: SSID too long. Truncating to 32 chars.");
        ssid = ssid.substring(0, SSID_MAX_LENGTH);
    }

    // パスワード文字数チェック
    if (password.length() > PASSWORD_MAX_LENGTH) {
        consoleLog("Warning: Password too long. Truncating to 63 chars.");
        password = password.substring(0, PASSWORD_MAX_LENGTH);
    }

    consoleLog("WiFi SSID loaded from SD: " + ssid);

    return true;
}

/**
 * @brief タイムゾーン名からUTC offsetを検索
 * @param timezoneName タイムゾーン名（例: "Asia/Tokyo", "asia/tokyo"）
 * @return offset値（秒単位）、見つからない場合はDEFAULT_TIMEZONE_OFFSET
 */
static int32_t findTimezoneOffset(const String &timezoneName) {
    // マッピングテーブルを線形探索
    for (int i = 0; i < timezoneTableSize; i++) {
        // PROGMEMから読み込み
        const char* tableName = (const char*)pgm_read_ptr(&timezoneTable[i].name);
        int32_t tableOffset = pgm_read_dword(&timezoneTable[i].offsetSeconds);

        // 大文字小文字を区別せずに比較
        String tableNameStr = String(tableName);
        if (timezoneName.equalsIgnoreCase(tableNameStr)) {
            return tableOffset;
        }
    }

    // 見つからない場合はデフォルト値
    return DEFAULT_TIMEZONE_OFFSET;
}

/**
 * @brief SDカードからタイムゾーン設定を読み込み
 * @param timezoneOffset タイムゾーンoffset（秒単位、出力パラメータ）
 * @return 成功時true、失敗時false
 */
bool loadTimezoneFromSD(int32_t &timezoneOffset) {
    // デフォルト値設定
    timezoneOffset = DEFAULT_TIMEZONE_OFFSET;

    // config.iniファイル存在確認（SD.begin()は既にwifi.iniで呼び出し済みを想定）
    if (!SD.exists(CONFIG_TIMEZONE_FILE_PATH)) {
        consoleLog("config.ini not found. Using default timezone: Asia/Tokyo (UTC+9)");
        return false;
    }

    // ファイルオープン
    File configFile = SD.open(CONFIG_TIMEZONE_FILE_PATH, FILE_READ);
    if (!configFile) {
        consoleLog("Failed to open config.ini.");
        return false;
    }

    // ファイルサイズチェック
    if (configFile.size() > CONFIG_FILE_MAX_SIZE) {
        consoleLog("config.ini file too large.");
        configFile.close();
        return false;
    }

    // ファイル解析（行ごと）
    bool found = false;
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();  // CRLF対応、前後空白削除

        // コメント行と空行をスキップ
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }

        // timezone=行を検索
        if (line.startsWith("timezone=")) {
            String timezoneName = line.substring(9);  // "timezone="の後
            timezoneName.trim();  // 前後空白削除

            if (timezoneName.length() > 0) {
                // タイムゾーン名がマッピングテーブルに存在するか確認
                bool foundInTable = false;
                for (int i = 0; i < timezoneTableSize; i++) {
                    const char* tableName = (const char*)pgm_read_ptr(&timezoneTable[i].name);
                    if (timezoneName.equalsIgnoreCase(String(tableName))) {
                        foundInTable = true;
                        break;
                    }
                }

                if (foundInTable) {
                    // タイムゾーン名からoffset検索
                    int32_t offset = findTimezoneOffset(timezoneName);
                    timezoneOffset = offset;
                    consoleLog("Timezone loaded from config.ini: " + timezoneName + " (UTC" + String(offset / 3600.0, 1) + ")");
                    found = true;
                    break;
                } else {
                    consoleLog("Unknown timezone: " + timezoneName + ". Using default: Asia/Tokyo (UTC+9)");
                }
            }
        }
    }

    configFile.close();

    if (!found) {
        consoleLog("No valid timezone found in config.ini. Using default: Asia/Tokyo (UTC+9)");
        return false;
    }

    return true;
}

/**
 * @brief タイムゾーン設定を取得（SD優先、フォールバックはデフォルト値）
 * @return タイムゾーンoffset（秒単位）
 */
int32_t getTimezoneConfig() {
    int32_t timezoneOffset = DEFAULT_TIMEZONE_OFFSET;

    if (!loadTimezoneFromSD(timezoneOffset)) {
        // SD読み込み失敗時はデフォルト値を使用（既にログ出力済み）
        timezoneOffset = DEFAULT_TIMEZONE_OFFSET;
    }

    return timezoneOffset;
}

/**
 * @brief Symbol network設定値をバリデーション
 * @param network ネットワーク種別文字列
 * @return 有効ならtrue、無効ならfalse
 */
static bool validateSymbolNetwork(const String &network) {
    if (network.equalsIgnoreCase("testnet") || network.equalsIgnoreCase("mainnet")) {
        return true;
    }
    consoleLog("Symbol Config Error: Invalid network value (must be testnet or mainnet)");
    return false;
}

/**
 * @brief Symbol node URL設定値をバリデーション
 * @param nodeUrl ノードURL文字列
 * @return 有効ならtrue、無効ならfalse
 */
static bool validateSymbolNodeUrl(const String &nodeUrl) {
    if (!nodeUrl.startsWith("https://")) {
        consoleLog("Symbol Config Error: Invalid node URL format (must start with https://)");
        return false;
    }
    if (nodeUrl.length() > SYMBOL_NODE_MAX_LENGTH) {
        consoleLog("Symbol Config Error: Malformed node URL");
        return false;
    }
    return true;
}

/**
 * @brief Symbol address設定値をバリデーション
 * @param address Symbolアドレス文字列
 * @return 有効ならtrue、無効ならfalse
 */
static bool validateSymbolAddress(const String &address) {
    if (address.length() != SYMBOL_ADDRESS_LENGTH) {
        consoleLog("Symbol Config Error: Invalid address format (must be 39 chars, start with N/T)");
        return false;
    }
    char firstChar = address.charAt(0);
    if (firstChar != 'N' && firstChar != 'T') {
        consoleLog("Symbol Config Error: Invalid address format (must be 39 chars, start with N/T)");
        return false;
    }
    return true;
}

/**
 * @brief Symbol public key設定値をバリデーション
 * @param pubKey 公開鍵文字列
 * @return 有効ならtrue、無効ならfalse
 */
static bool validateSymbolPubKey(const String &pubKey) {
    if (pubKey.length() != SYMBOL_PUBKEY_LENGTH) {
        consoleLog("Symbol Config Error: Invalid public key format (must be 64 hex chars)");
        return false;
    }
    // 16進数文字チェック
    for (unsigned int i = 0; i < pubKey.length(); i++) {
        char c = pubKey.charAt(i);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            consoleLog("Symbol Config Error: Invalid public key format (must be 64 hex chars)");
            return false;
        }
    }
    return true;
}

/**
 * @brief Symbolネットワークとアドレスの整合性チェック
 * @param network ネットワーク種別
 * @param address Symbolアドレス
 */
static void checkNetworkAddressConsistency(const String &network, const String &address) {
    if (address.length() == 0) {
        return;  // アドレスが空の場合はチェックスキップ
    }

    char firstChar = address.charAt(0);
    if (network.equalsIgnoreCase("testnet") && firstChar != 'T') {
        consoleLog("Symbol Config Warning: Network/address mismatch (testnet expects T prefix)");
    } else if (network.equalsIgnoreCase("mainnet") && firstChar != 'N') {
        consoleLog("Symbol Config Warning: Network/address mismatch (mainnet expects N prefix)");
    }
}

void getWiFiCredentials(String &ssid, String &password) {
    if (!loadWiFiConfigFromSD(ssid, password)) {
        // SD読み込み失敗時はソースコード定数を使用
        ssid = WIFI_SSID;
        password = WIFI_PASSWORD;
        consoleLog("Using hardcoded WiFi settings.");
    }
}

bool connectToWiFi(const String &ssid, const String &password) {
    consoleLog("Connecting to WiFi...");

    WiFi.begin(ssid.c_str(), password.c_str());

    wl_status_t result = (wl_status_t)WiFi.waitForConnectResult(WIFI_CONNECT_TIMEOUT);

    if (result == WL_CONNECTED) {
        consoleLog("WiFi connected. IP: " + WiFi.localIP().toString());
        return true;
    } else {
        consoleLog("WiFi connection failed. Operating without network.");
        return false;
    }
}

bool syncNTP(int32_t timezoneOffset) {
    consoleLog("Syncing NTP time...");

    // NTP設定（サーバー、タイムゾーン、夏時間オフセット）
    configTime(timezoneOffset, 0, NTP_SERVER);

    // NTP同期完了を待つ（タイムアウト付き）
    unsigned long startTime = millis();
    time_t now = 0;

    while (now < 100000) {  // 1970年以降の妥当な時刻かチェック
        time(&now);

        if (millis() - startTime > NTP_SYNC_TIMEOUT) {
            consoleLog("NTP sync timeout.");
            return false;
        }

        if (now > 100000) {
            break;
        }

        delay(100);
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        consoleLog("Failed to get local time.");
        return false;
    }

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    consoleLog("NTP synced: " + String(timeStr));

    return true;
}

/**
 * @brief SDカードからSymbol設定を読み込み
 * @param config Symbol設定構造体（出力パラメータ）
 * @return 成功時true、失敗時false
 */
bool loadSymbolConfigFromSD(SymbolConfig &config) {
    // config.iniファイル存在確認（SD.begin()は既にwifi.iniで呼び出し済みを想定）
    if (!SD.exists(CONFIG_TIMEZONE_FILE_PATH)) {
        consoleLog("config.ini not found. Using hardcoded Symbol config.");
        return false;
    }

    // ファイルオープン
    File configFile = SD.open(CONFIG_TIMEZONE_FILE_PATH, FILE_READ);
    if (!configFile) {
        consoleLog("Symbol Config Error: Corrupted config file. Using hardcoded fallback.");
        return false;
    }

    // ファイルサイズチェック
    if (configFile.size() > CONFIG_FILE_MAX_SIZE) {
        consoleLog("Symbol Config Error: Corrupted config file. Using hardcoded fallback.");
        configFile.close();
        return false;
    }

    // ファイル解析ループ
    bool found = false;
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();  // CRLF対応、前後空白削除

        // コメント行と空行をスキップ
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }

        // network=行を検索
        if (line.startsWith("network=")) {
            String value = line.substring(8);  // "network="の後
            value.trim();
            if (value.length() > 0) {
                config.network = value;
                found = true;
            }
        }

        // node=行を検索
        if (line.startsWith("node=")) {
            String value = line.substring(5);  // "node="の後
            value.trim();
            if (value.length() > 0) {
                config.node = value;
                found = true;
            }
        }

        // address=行を検索
        if (line.startsWith("address=")) {
            String value = line.substring(8);  // "address="の後
            value.trim();
            if (value.length() > 0) {
                config.address = value;
                found = true;
            }
        }

        // pubKey=行を検索
        if (line.startsWith("pubKey=")) {
            String value = line.substring(7);  // "pubKey="の後
            value.trim();
            if (value.length() > 0) {
                config.pubKey = value;
                found = true;
            }
        }
    }

    configFile.close();

    if (!found) {
        consoleLog("No valid Symbol config found in config.ini. Using hardcoded values.");
        return false;
    }

    // バリデーション処理
    bool isValid = true;

    // network検証
    if (config.network.length() > 0 && !validateSymbolNetwork(config.network)) {
        config.network = SYMBOL_DEFAULT_NETWORK;
        isValid = false;
    }

    // node URL検証
    if (config.node.length() > 0 && !validateSymbolNodeUrl(config.node)) {
        config.node = SYMBOL_DEFAULT_NODE;
        isValid = false;
    }

    // address検証
    if (config.address.length() > 0 && !validateSymbolAddress(config.address)) {
        config.address = SYMBOL_DEFAULT_ADDRESS;
        isValid = false;
    }

    // pubKey検証
    if (config.pubKey.length() > 0 && !validateSymbolPubKey(config.pubKey)) {
        config.pubKey = SYMBOL_DEFAULT_PUBKEY;
        isValid = false;
    }

    // ネットワークとアドレスの整合性チェック
    checkNetworkAddressConsistency(config.network, config.address);

    // 成功ログ出力（address/pubKeyは表示しない）
    if (found) {
        consoleLog("Symbol config loaded from SD: network=" + config.network + ", node=" + config.node);
    }

    return true;
}

/**
 * @brief Symbol設定を取得（SD優先、フォールバックはハードコード値）
 * @return Symbol設定構造体
 */
SymbolConfig getSymbolConfig() {
    SymbolConfig config;

    // ハードコード値で初期化
    config.network = SYMBOL_DEFAULT_NETWORK;
    config.node = SYMBOL_DEFAULT_NODE;
    config.address = SYMBOL_DEFAULT_ADDRESS;
    config.pubKey = SYMBOL_DEFAULT_PUBKEY;

    // SDカードから読み込み試行
    if (!loadSymbolConfigFromSD(config)) {
        // SD読み込み失敗時はハードコード値を使用（既にログ出力済み）
        consoleLog("Using hardcoded Symbol config (testnet)");
        config.network = SYMBOL_DEFAULT_NETWORK;
        config.node = SYMBOL_DEFAULT_NODE;
        config.address = SYMBOL_DEFAULT_ADDRESS;
        config.pubKey = SYMBOL_DEFAULT_PUBKEY;
    }

    return config;
}
