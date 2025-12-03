/**
 * @file websocket.h
 * @brief Symbol blockchain WebSocket接続と地震データ監視機能
 */

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>
#include "network.h"  // SymbolConfig構造体を使用

/**
 * @brief WebSocket機能を初期化
 * @param config Symbol設定（network, node, address, pubKey）
 * @details WebSocketクライアントのセットアップ、イベントハンドラー登録、重複検出バッファ初期化
 */
void initWebSocket(const SymbolConfig &config);

/**
 * @brief WebSocketループ処理（loop()から呼び出し）
 * @details 接続状態確認、メッセージ受信、再接続処理、メモリ監視
 */
void webSocketLoop();

/**
 * @brief WebSocket接続状態を取得
 * @return 接続中ならtrue、切断中ならfalse
 */
bool getWebSocketConnected();

#endif // WEBSOCKET_H
