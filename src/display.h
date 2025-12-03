/**
 * @file display.h
 * @brief 地震情報画面表示機能のヘッダーファイル
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "earthquake.h"

/**
 * @brief 地震情報表示機能を初期化
 * @details リストデータ構造を初期化し、初期メッセージを表示
 */
void initDisplay();

/**
 * @brief 地震情報表示を更新（loop()から呼び出し）
 * @details タッチ処理、スクロール、リスト描画を実行
 */
void updateDisplay();

/**
 * @brief WebSocketから受信した新規地震情報をリストに追加
 * @param data 地震情報データ
 */
void addEarthquakeToDisplay(const EarthquakeData& data);

/**
 * @brief 震度に応じた背景色を取得
 * @param intensity 震度文字列（"1", "2", "3", "4", "5弱", "5強", "6弱", "6強", "7"）
 * @return 背景色（uint16_t RGB565形式）
 * @details notification.cppから視覚通知の点滅色として使用
 */
uint16_t getIntensityColor(const String& intensity);

/**
 * @brief ユーザーがスクロール中かを判定
 * @return スクロール中ならtrue、停止中ならfalse
 * @details isDragging || scrollOffset > 0 の場合にtrueを返す
 */
bool isUserScrolling();

/**
 * @brief 地震情報リストを画面に描画
 * @details notification.cppから視覚通知終了時に呼び出し
 */
void renderList();

#endif // DISPLAY_H
