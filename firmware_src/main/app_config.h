/**
 * @file app_config.h
 * @brief 应用配置定义
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 设备配置 */
#define APP_DEVICE_NAME         CONFIG_MIDI_DEVICE_NAME
#define APP_PRODUCT_ID          CONFIG_MIDI_PRODUCT_ID
#define APP_LISTEN_PORT         CONFIG_MIDI_LISTEN_PORT

/* 任务栈大小 */
#define APP_SESSION_MONITOR_STACK   2048
#define APP_MIDI_SEND_STACK         2048
#define APP_EVENT_HANDLER_STACK     3072

/* 任务优先级 */
#define APP_SESSION_MONITOR_PRIORITY    5
#define APP_MIDI_SEND_PRIORITY          5
#define APP_EVENT_HANDLER_PRIORITY      6

/* 定时配置 */
#define APP_SESSION_CHECK_INTERVAL_MS   1000
#define APP_MIDI_SEND_INTERVAL_MS       1000

#endif // APP_CONFIG_H