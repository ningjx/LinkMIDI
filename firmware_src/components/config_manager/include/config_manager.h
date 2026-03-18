/**
 * @file config_manager.h
 * @brief 配置管理器接口
 * 
 * 提供设备配置的持久化存储功能，支持：
 * - WiFi 凭证保存
 * - MIDI 设备配置
 * - 运行时参数调整
 * - 恢复出厂设置
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi 配置结构
 */
typedef struct {
    char ssid[32];          /**< WiFi SSID */
    char password[64];      /**< WiFi 密码 */
    uint8_t max_retry;      /**< 最大重试次数 */
    bool auto_connect;      /**< 是否自动连接 */
} wifi_config_data_t;

/**
 * @brief MIDI 设备配置结构
 */
typedef struct {
    char device_name[32];   /**< 设备名称 */
    char product_id[32];    /**< 产品 ID */
    uint16_t listen_port;   /**< 监听端口 */
    uint8_t device_mode;    /**< 设备模式: 0=client, 1=server, 2=peer */
    bool enable_discovery;  /**< 是否启用 mDNS 发现 */
} midi_config_data_t;

/**
 * @brief 系统配置结构
 */
typedef struct {
    wifi_config_data_t wifi;    /**< WiFi 配置 */
    midi_config_data_t midi;    /**< MIDI 配置 */
    uint32_t config_version;    /**< 配置版本号 */
    bool is_configured;         /**< 是否已配置 */
} system_config_t;

/**
 * @brief 初始化配置管理器
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_init(void);

/**
 * @brief 反初始化配置管理器
 */
void config_manager_deinit(void);

/**
 * @brief 加载配置从 NVS
 * @param config 配置结构指针
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_load(system_config_t* config);

/**
 * @brief 保存配置到 NVS
 * @param config 配置结构指针
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_save(const system_config_t* config);

/**
 * @brief 获取默认配置
 * @param config 配置结构指针
 */
void config_manager_get_defaults(system_config_t* config);

/**
 * @brief 恢复出厂设置
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_factory_reset(void);

/**
 * @brief 检查是否已配置
 * @return true 已配置，false 未配置
 */
bool config_manager_is_configured(void);

/**
 * @brief 更新 WiFi 配置
 * @param wifi_config WiFi 配置指针
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_update_wifi(const wifi_config_data_t* wifi_config);

/**
 * @brief 更新 MIDI 配置
 * @param midi_config MIDI 配置指针
 * @return MIDI_OK 成功，其他值失败
 */
midi_error_t config_manager_update_midi(const midi_config_data_t* midi_config);

/**
 * @brief 获取当前配置
 * @return 当前配置指针（只读）
 */
const system_config_t* config_manager_get_current(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H