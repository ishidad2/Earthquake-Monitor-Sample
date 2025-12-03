# M5Stack Core2 地震モニター

M5Stack Core2を用いた地震モニタリングシステム。Symbol blockchainから地震情報を取得し、リアルタイムで表示・通知します。

## 機能

### 画面表示

#### ヘッダー（常時表示）

画面上部30pxのヘッダー領域に以下の情報を表示：

```
[Time Display]              [WS]  [WiFi]
YYYY/MM/DD HH:MM            接続   接続状態
```

- **時刻表示** (X: 120, 中央寄せ): NTP同期による正確な日時（`YYYY/MM/DD HH:MM`形式）
  - NTP未同期時は "No Time Data" を表示
  - 1分ごとに自動更新
  - 時刻更新時、WebSocketインジケーターを自動的に再描画（干渉防止）
- **WebSocket接続状態** (X: 240): "WS"テキストインジケーター
  - 緑色: Symbol blockchainに接続中
  - グレー: 切断中
  - 差分描画により、接続状態変化時のみ再描画
- **WiFi接続状態** (X: 295): 携帯電波マークアイコン（3本の縦線）
  - 緑色: WiFiネットワークに接続中
  - グレー + 斜線: 接続失敗または切断

**レイアウト最適化:**
- ヘッダー要素間に最低5pxのマージンを確保
- 時刻表示のクリア範囲を動的に計算し、WebSocketインジケーターと重ならないように境界チェック
- タイトル表示は無効化（`SHOW_HEADER_TITLE false`）

#### 起動画面
- **アプリケーション名**: "Earthquake Monitor"
- **バージョン表示**: 現在のファームウェアバージョン（v1.0.0）
- **プログレスバー**: 初期化進捗を視覚的に表示
- **ステータスメッセージ**: WiFi接続、NTP同期の状態を表示

#### メイン画面
- **地震情報リスト**: 最大50件の地震情報を新しい順に表示
  - 震度、震源地、マグニチュード、発生時刻
  - 震度別の色分け表示
- **タッチ操作**: 上下スワイプでリストをスクロール（PAGE_SIZE単位でページング）

### 通知機能

- **音声通知**: 震度に応じたビープ音（1-3回、各150ms）で新規地震を通知
  - 震度1-2: 1回
  - 震度3-4: 2回
  - 震度5弱以上: 3回
- **視覚通知**: 震度に応じた色で画面を点滅（1.5秒間、300ms間隔）
  - 震度1-2: 緑
  - 震度3-4: 黄
  - 震度5弱-6弱: 橙
  - 震度6強-7: 赤
- **通知キュー**: 最大3件の地震情報を順次処理（先入先出）
- **通知キャンセル**: 画面点滅中にタッチすると通知を中断

### データ管理

- **リアルタイム受信**: Symbol blockchain WebSocketから地震情報を受信
- **重複検出**: トランザクションハッシュで重複をチェック（最新10件を保持）
- **署名者フィルタリング**: 設定された公開鍵からのトランザクションのみを処理（オプション）
- **データ検証**: 必須フィールドのチェック、不正データのスキップ

### ネットワーク機能

- **WiFi接続**: SDカードから設定を読み込み、自動接続
- **NTP時刻同期**: タイムゾーン設定に基づく正確な時刻表示
- **WebSocket接続**: Symbol blockchainノードへの常時接続
- **REST API**: 起動時に過去の地震情報を取得（PAGE_SIZE件、デフォルト30件）

### 設定管理

- **SDカード設定**: `/wifi.ini`,`/config.ini`から設定を読み込み
  - WiFi設定（SSID、パスワード）
  - Symbol設定（ネットワーク、ノード、アドレス、公開鍵）
  - タイムゾーン設定（オプション、デフォルト: +9時間）

## 地震情報JSON仕様

### 1. WebSocket受信メッセージ（Symbol blockchain）

Symbol blockchainのWebSocketから受信するメッセージ構造：

```json
{
  "topic": "confirmedAdded/<address>",
  "data": {
    "meta": {
      "hash": "トランザクションハッシュ（64文字16進数）",
      "height": "ブロック高",
      "timestamp": "タイムスタンプ"
    },
    "transaction": {
      "type": 16724,
      "signerPublicKey": "署名者公開鍵（64文字16進数）",
      "message": "16進数エンコードされた地震情報JSON",
      "recipientAddress": "受信アドレス"
    }
  }
}
```

#### 重要なフィールド

- **`data.meta.hash`**: トランザクションハッシュ。重複検出に使用
- **`data.transaction.signerPublicKey`**: 署名者の公開鍵。設定ファイル（config.json）の`pubKey`と照合してフィルタリング
- **`data.transaction.message`**: 16進数エンコードされた地震情報JSON（次のセクション参照）

### 2. 地震情報JSON（16進数デコード後）

トランザクションの`message`フィールドを16進数デコードすると、以下の地震情報JSONが得られます：

```json
{
  "earthquake": {
    "time": "2024-12-03T14:30:00+09:00",
    "hypocenter": {
      "name": "茨城県南部",
      "latitude": 36.1,
      "longitude": 140.1,
      "depth": 50,
      "magnitude": 4.5
    },
    "maxScale": 45,
    "domesticTsunami": "None"
  }
}
```

#### フィールド説明

| フィールド | 型 | 必須 | 説明 | 例 |
|-----------|-----|------|------|-----|
| `earthquake.time` | String | ✓ | 地震発生時刻（ISO8601形式） | `"2024-12-03T14:30:00+09:00"` |
| `earthquake.hypocenter.name` | String | ✓ | 震源地名 | `"茨城県南部"` |
| `earthquake.hypocenter.latitude` | Float | ✓ | 緯度（度） | `36.1` |
| `earthquake.hypocenter.longitude` | Float | ✓ | 経度（度） | `140.1` |
| `earthquake.hypocenter.depth` | Integer | ✓ | 深さ（km） | `50` |
| `earthquake.hypocenter.magnitude` | Float | ✓ | マグニチュード | `4.5` |
| `earthquake.maxScale` | Integer | ✓ | 最大震度（後述の震度コード表参照） | `45` |
| `earthquake.domesticTsunami` | String | ✓ | 津波情報（英語キーワード） | `"None"`, `"NonEffective"`, `"Watch"`, `"Warning"`, `"MajorWarning"` |

### 3. 震度コード（maxScale）対応表

`maxScale`フィールドは整数値で震度を表現します：

| maxScale | 震度 | 表示色 | ビープ音回数 |
|----------|------|--------|------------|
| 10 | 1 | 緑 | 1回 |
| 20 | 2 | 緑 | 1回 |
| 30 | 3 | 黄 | 2回 |
| 40 | 4 | 黄 | 2回 |
| 45 | 5弱 | 橙 | 3回 |
| 50 | 5強 | 橙 | 3回 |
| 55 | 6弱 | 橙 | 3回 |
| 60 | 6強 | 赤 | 3回 |
| 70 | 7 | 赤 | 3回 |

### 4. 津波情報コード（domesticTsunami）対応表

`domesticTsunami`フィールドは英語のキーワードで津波情報を表現します：

| キーワード | 意味 | 説明 |
|-----------|------|------|
| None | 津波の心配なし | 津波の影響はありません |
| NonEffective | 若干の海面変動の可能性 | 若干の海面変動が発生する可能性がありますが、被害の心配はありません |
| Watch | 津波注意報 | 海の中や海岸付近は危険です |
| Warning | 津波警報 | 沿岸部や川沿いにいる人は、ただちに高台や避難ビルなど安全な場所へ避難してください |
| MajorWarning | 大津波警報 | 沿岸部や川沿いにいる人は、ただちに高台や避難ビルなど安全な場所へ避難してください |

**注**: 現在の実装では、英語のキーワードをそのままログ出力しています。日本語表示が必要な場合は、変換処理を追加できます。

### 5. JSON例（実際のデータ）

#### 震度5弱の例

```json
{
  "earthquake": {
    "time": "2024-03-15T09:45:23+09:00",
    "hypocenter": {
      "name": "千葉県北西部",
      "latitude": 35.7,
      "longitude": 140.2,
      "depth": 80,
      "magnitude": 5.8
    },
    "maxScale": 45,
    "domesticTsunami": "None"
  }
}
```

この例では：
- 震度5弱（`maxScale: 45`）
- ビープ音3回
- 画面が橙色で点滅
- リストの先頭に追加される

#### 震度3の例

```json
{
  "earthquake": {
    "time": "2024-03-15T12:30:00+09:00",
    "hypocenter": {
      "name": "茨城県南部",
      "latitude": 36.1,
      "longitude": 140.1,
      "depth": 50,
      "magnitude": 4.2
    },
    "maxScale": 30,
    "domesticTsunami": "None"
  }
}
```

この例では：
- 震度3（`maxScale: 30`）
- ビープ音2回
- 画面が黄色で点滅

### 5. 16進数エンコード/デコード

Symbol blockchainのトランザクションメッセージは16進数エンコードされています。

#### デコード手順

1. トランザクションの`message`フィールドから16進数文字列を取得
2. 最初の1バイト（`00`）をスキップ
3. 残りの16進数文字列を2文字ずつUTF-8バイトに変換
4. UTF-8文字列として解釈し、JSONパース

#### 例

**16進数メッセージ**:
```
007b226561727468717561...（省略）
```

**デコード後のJSON**:
```json
{"earthquake":{"time":"2024-03-15T09:45:23+09:00",...}}
```

## データ検証

### 必須フィールドチェック

以下のフィールドが欠損している場合、データは無効として扱われます：

- `earthquake.time`
- `earthquake.hypocenter` (オブジェクト)
- `earthquake.maxScale`
- `earthquake.hypocenter.name` (空文字列の場合も無効)

### データフィルタリング

以下の条件に該当するデータはスキップされます：

- 震度が不明（`maxScale`が定義範囲外）
- 震源地名が空文字列
- マグニチュードが負の値
- 重複トランザクション（過去10件のハッシュと照合）

## 設定ファイル（config.json）

### SDカード内の`/wifi.ini`で以下を設定：

```
YourWiFiSSID
YourWiFiPassword

```

wifi接続のためのSSIDとパスワードを記載します。

### SDカード内の`/config.ini`で以下を設定：

```
# Timezone configuration for M5Stack Earthquake Monitor
# タイムゾーン設定ファイル
#
# このファイルをSDカードのルートディレクトリに「config.ini」という名前でコピーしてください。
# Copy this file to SD card root directory as "config.ini"

# タイムゾーン設定
# Timezone configuration (case-insensitive)
timezone=Asia/Tokyo

# サポートされているタイムゾーン一覧
# Supported timezones:
#
# アジア (Asia):
#   Asia/Tokyo       (UTC+9)  - 日本標準時
#   Asia/Shanghai    (UTC+8)  - 中国標準時
#   Asia/Singapore   (UTC+8)  - シンガポール標準時
#   Asia/Hong_Kong   (UTC+8)  - 香港標準時
#   Asia/Seoul       (UTC+9)  - 韓国標準時
#   Asia/Bangkok     (UTC+7)  - タイ標準時
#   Asia/Dubai       (UTC+4)  - UAEガルフ標準時
#   Asia/Kolkata     (UTC+5.5)- インド標準時
#
# アメリカ (Americas):
#   America/New_York     (UTC-5) - 米国東部標準時
#   America/Chicago      (UTC-6) - 米国中部標準時
#   America/Denver       (UTC-7) - 米国山岳部標準時
#   America/Los_Angeles  (UTC-8) - 米国太平洋標準時
#   America/Sao_Paulo    (UTC-3) - ブラジル標準時
#
# ヨーロッパ (Europe):
#   Europe/London    (UTC+0)  - グリニッジ標準時
#   Europe/Paris     (UTC+1)  - 中央ヨーロッパ標準時
#   Europe/Berlin    (UTC+1)  - 中央ヨーロッパ標準時
#   Europe/Moscow    (UTC+3)  - モスクワ標準時
#
# オセアニア (Oceania):
#   Pacific/Auckland (UTC+12) - ニュージーランド標準時
#   Australia/Sydney (UTC+10) - オーストラリア東部標準時
#
# その他 (Others):
#   UTC              (UTC+0)  - 協定世界時
#   GMT              (UTC+0)  - グリニッジ標準時
#
# 注意事項:
# - 大文字小文字は区別されません (asia/tokyo でも ASIA/TOKYO でも可)
# - 夏時間（サマータイム）には対応していません（標準時のみ）
# - サポートされていないタイムゾーンを指定すると、デフォルトのAsia/Tokyoが使用されます

# Symbol blockchain configuration for M5Stack Earthquake Monitor
# Symbol設定ファイル（地震モニターシステム用）
#
# このセクションはSymbolブロックチェーン連携機能の設定です（オプション）
# Symbol blockchain integration settings (optional)

# ネットワーク種別 (Network type)
# testnet: テストネット (test network)
# mainnet: メインネット (main network)
network=testnet

# ノードURL (Node URL)
# 必ずhttps://で始まること (must start with https://)
# 最大200文字 (max 200 characters)
node=https://sym-test-03.opening-line.jp:3001

# Symbolアドレス (Symbol address)
# 39文字、testnetは'T'で始まり、mainnetは'N'で始まる
# 39 characters, testnet starts with 'T', mainnet starts with 'N'
# 例 (Example): TBIL6D6RURP45YQRWV6Q7YNBQGGDY4ZTYS4K7NI
address=

# 公開鍵 (Public key)
# 64文字の16進数 (64 hex characters)
# 例 (Example): 1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF
pubKey=

# 注意事項 (Notes):
# - address と pubKey は空のままでもシステムは動作します
#   (System works even if address and pubKey are empty)
# - 秘密鍵(private key)は絶対にこのファイルに記載しないでください
#   (NEVER write your private key in this file)
# - networkとaddressの先頭文字が一致していることを確認してください
#   testnet → T, mainnet → N
#   (Ensure network and address prefix match: testnet → T, mainnet → N)
```

### 設定項目の詳細

| 項目 | 必須 | 説明 | デフォルト値 |
|------|------|------|------------|
| `network` | ✓ | Symbol Network ("testnet" or "mainnet") | mainnet |
| `node` | ✓ | Symbol NodeのURL | 指定がなければソースコード埋め込み値を使用 |
| `address` | ✓ | 監視対象のSymbolアドレス | 指定がなければソースコード埋め込み値を使用 |
| `pubKey` | - | 署名者公開鍵（フィルタリング用） | 指定がなければソースコード埋め込み値を使用 |
| `timezone` | - | タイムゾーン | `Asia/Tokyo`（日本標準時） |

## 通知動作

### 新規地震検出時の動作フロー

1. **WebSocket受信**: Symbol blockchainから新規トランザクションを受信
2. **重複検出**: トランザクションハッシュで重複チェック（最新10件を保持）
3. **署名者検証**: 設定された公開鍵と照合
4. **JSONパース**: 16進数メッセージをデコードし、地震情報JSONをパース
5. **通知キュー追加**: 最大3件のキューに追加
6. **通知処理**:
   - ビープ音再生（震度別1-3回、各150ms）
   - 画面点滅（震度別色、1.5秒間、300ms間隔でON/OFF）
   - リスト先頭に追加（最大50件、古い順に削除）
7. **次の通知**: キュー内に通知がある場合、順次処理

### 通知のキャンセル

- **タッチ操作**: 画面点滅中にタッチすると、即座に通知を中断
- **スクロール位置維持**: ユーザーがスクロール中の場合、自動スクロールしない

## ビルドとアップロード

```bash
# ビルド
pio run

# デバイスにアップロード
pio run --target upload

# シリアルモニタ
pio device monitor
```

## 技術的な実装詳細

### 差分描画によるパフォーマンス最適化

ヘッダー表示の更新は、静的変数による状態管理で差分描画を実現：

- **WiFi状態**: `lastWiFiState`で前回の接続状態を保持し、変化時のみ再描画
- **WebSocket状態**: `lastWsState`で前回の接続状態を保持し、変化時のみ再描画
- **時刻表示**: `lastTimeStr`で前回の時刻文字列を保持し、変化時のみ再描画

この最適化により：
- 不要な描画を削減し、画面のちらつき（フリッカー）を防止
- ヘッダー更新処理を10ms以内に維持（ESP32の制約下でもスムーズな動作）

## 変更履歴

### v1.0.0 (2024-12-03)
- 初回リリース
- Symbol blockchainからの地震情報受信機能
- リアルタイム表示、音声・視覚通知機能
