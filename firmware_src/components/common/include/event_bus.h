/**
 * @file event_bus.h
 * @brief 事件总线 - 模块间解耦通信
 * 
 * 提供发布/订阅模式的事件通信机制，解决模块间直接依赖问题。
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 事件类型定义
 * ============================================================================ */

/**
 * @brief 事件类型枚举
 */
typedef enum {
    /* MIDI 数据事件 */
    EVENT_MIDI_DATA_RECEIVED,       ///< MIDI 数据接收 (来自 USB 或网络)
    EVENT_UMP_DATA_RECEIVED,        ///< UMP 数据接收 (来自网络)
    
    /* USB 事件 */
    EVENT_USB_DEVICE_CONNECTED,     ///< USB MIDI 设备连接
    EVENT_USB_DEVICE_DISCONNECTED,  ///< USB MIDI 设备断开
    EVENT_USB_ERROR,                ///< USB 错误
    
    /* 会话事件 */
    EVENT_SESSION_ESTABLISHED,      ///< MIDI 2.0 会话建立
    EVENT_SESSION_TERMINATED,       ///< MIDI 2.0 会话终止
    EVENT_SESSION_ERROR,            ///< 会话错误
    
    /* WiFi 事件 */
    EVENT_WIFI_CONNECTED,           ///< WiFi 已连接
    EVENT_WIFI_DISCONNECTED,        ///< WiFi 已断开
    EVENT_WIFI_ERROR,               ///< WiFi 错误
    
    /* 发现事件 */
    EVENT_DEVICE_DISCOVERED,        ///< 发现新设备
    EVENT_DEVICE_LOST,              ///< 设备离线
    
    /* 系统事件 */
    EVENT_SYSTEM_ERROR,             ///< 系统错误
    EVENT_CONFIG_CHANGED,           ///< 配置变更
    
} event_type_t;

/* ============================================================================
 * 事件数据结构
 * ============================================================================ */

/**
 * @brief MIDI 数据事件载荷
 */
typedef struct {
    uint8_t source;         ///< 数据来源: 0=USB, 1=Network
    uint8_t device_index;   ///< USB 设备索引 (source=0 时有效)
    const uint8_t* data;    ///< MIDI 数据指针 (不持有所有权)
    uint16_t length;        ///< 数据长度
} event_midi_data_t;

/**
 * @brief UMP 数据事件载荷
 */
typedef struct {
    const uint8_t* data;    ///< UMP 数据指针 (不持有所有权)
    uint16_t length;        ///< 数据长度
} event_ump_data_t;

/**
 * @brief USB 设备信息
 */
typedef struct {
    uint8_t device_index;       ///< 设备索引
    uint16_t vendor_id;         ///< USB VID
    uint16_t product_id;        ///< USB PID
    char manufacturer[64];      ///< 制造商
    char product_name[64];      ///< 产品名
} event_usb_device_t;

/**
 * @brief 会话状态事件载荷
 */
typedef struct {
    uint32_t remote_ssrc;       ///< 远端 SSRC
    char remote_name[64];       ///< 远端设备名
    midi_error_t error_code;    ///< 错误码 (错误事件时有效)
} event_session_t;

/**
 * @brief WiFi 状态事件载荷
 */
typedef struct {
    uint8_t reason;             ///< 断开原因 (WiFi 事件时有效)
    midi_error_t error_code;    ///< 错误码
} event_wifi_t;

/**
 * @brief 发现设备信息
 */
typedef struct {
    char name[64];              ///< 设备名
    char product_id[64];        ///< 产品 ID
    uint32_t ip_addr;           ///< IP 地址
    uint16_t port;              ///< 端口
} event_discovered_device_t;

/**
 * @brief 错误事件载荷
 */
typedef struct {
    event_type_t source_event;  ///< 原始事件类型
    midi_error_t error_code;    ///< 错误码
    const char* message;        ///< 错误消息
} event_error_t;

/**
 * @brief 事件结构体
 */
typedef struct {
    event_type_t type;          ///< 事件类型
    uint32_t timestamp;         ///< 时间戳 (ms)
    union {
        event_midi_data_t midi;         ///< MIDI 数据
        event_ump_data_t ump;           ///< UMP 数据
        event_usb_device_t usb_device;  ///< USB 设备
        event_session_t session;        ///< 会话
        event_wifi_t wifi;              ///< WiFi
        event_discovered_device_t device; ///< 发现设备
        event_error_t error;            ///< 错误
    } data;
} event_t;

/* ============================================================================
 * 回调类型定义
 * ============================================================================ */

/**
 * @brief 事件回调函数类型
 * @param event 事件指针 (回调期间有效，如需保存请复制数据)
 * @param user_data 用户数据
 */
typedef void (*event_callback_t)(const event_t* event, void* user_data);

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/**
 * @brief 初始化事件总线
 * @return MIDI_OK 成功
 */
midi_error_t event_bus_init(void);

/**
 * @brief 反初始化事件总线
 * @return MIDI_OK 成功
 */
midi_error_t event_bus_deinit(void);

/**
 * @brief 检查事件总线是否已初始化
 * @return true 已初始化
 */
bool event_bus_is_initialized(void);

/**
 * @brief 订阅事件
 * @param type 事件类型
 * @param callback 回调函数
 * @param user_data 用户数据 (将传递给回调)
 * @return 订阅 ID (用于取消订阅)，0 表示失败
 */
uint32_t event_bus_subscribe(event_type_t type, event_callback_t callback, void* user_data);

/**
 * @brief 取消订阅
 * @param subscription_id 订阅 ID
 * @return MIDI_OK 成功
 */
midi_error_t event_bus_unsubscribe(uint32_t subscription_id);

/**
 * @brief 发布事件
 * @param event 事件指针 (数据会被复制)
 * @return MIDI_OK 成功
 * @note 此函数会同步调用所有订阅者的回调
 */
midi_error_t event_bus_publish(const event_t* event);

/* ============================================================================
 * 便捷发布函数
 * ============================================================================ */

/**
 * @brief 发布 MIDI 数据接收事件
 */
midi_error_t event_bus_publish_midi_data(uint8_t source, uint8_t device_index,
                                          const uint8_t* data, uint16_t length);

/**
 * @brief 发布 UMP 数据接收事件
 */
midi_error_t event_bus_publish_ump_data(const uint8_t* data, uint16_t length);

/**
 * @brief 发布 USB 设备连接事件
 */
midi_error_t event_bus_publish_usb_connected(uint8_t device_index, uint16_t vid, uint16_t pid,
                                              const char* manufacturer, const char* product_name);

/**
 * @brief 发布 USB 设备断开事件
 */
midi_error_t event_bus_publish_usb_disconnected(uint8_t device_index);

/**
 * @brief 发布会话建立事件
 */
midi_error_t event_bus_publish_session_established(uint32_t remote_ssrc, const char* remote_name);

/**
 * @brief 发布会话终止事件
 */
midi_error_t event_bus_publish_session_terminated(void);

/**
 * @brief 发布 WiFi 连接事件
 */
midi_error_t event_bus_publish_wifi_connected(void);

/**
 * @brief 发布 WiFi 断开事件
 */
midi_error_t event_bus_publish_wifi_disconnected(uint8_t reason);

/**
 * @brief 发布设备发现事件
 */
midi_error_t event_bus_publish_device_discovered(const char* name, const char* product_id,
                                                  uint32_t ip_addr, uint16_t port);

/**
 * @brief 发布错误事件
 */
midi_error_t event_bus_publish_error(event_type_t source, midi_error_t code, const char* message);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H