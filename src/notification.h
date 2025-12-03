/**
 * @file notification.h
 * @brief 地震通知機能のインターフェース定義
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include "earthquake.h"
#include <M5Unified.h>

/**
 * @brief 通知機能を初期化（M5.Speaker初期化、状態変数リセット）
 * @details setup()から呼び出す。M5.begin()実行後に呼び出すこと
 */
void initNotification();

/**
 * @brief 新規地震情報を通知キューに追加
 * @param data 地震情報データ
 * @details WebSocketメッセージ受信時に呼び出す。キューに追加し、順次処理を行う
 * @note 重複検出はwebsocket.cpp層で完了済みと想定
 */
void notifyEarthquake(const EarthquakeData& data);

/**
 * @brief 通知処理を更新（ノンブロッキング音声再生用）
 * @details loop()から毎回呼び出す。音声再生状態マシン、視覚通知、キュー処理を更新
 */
void updateNotification();

#endif // NOTIFICATION_H
