#include "usb_midi_host.h"
#include "esp_log.h"
#include "esp_check.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "USB_MIDI_HOST";

#define MAX_MIDI_DEVICES 4
#define MIDI_RX_QUEUE_SIZE 32
#define USB_HOST_PRIORITY 5
#define MIDI_DEVICE_STATE_IDLE 0
#define MIDI_DEVICE_STATE_ACTIVE 1
#define MIDI_DEVICE_STATE_CONFIGURED 2

/**
 * @brief MIDI device instance
 */
typedef struct {
    uint32_t dev_addr;
    usb_device_handle_t usb_dev;
    usb_midi_device_t device_info;
    uint8_t config_desc_index;
    uint32_t state;
    TaskHandle_t transfer_task;
    usb_transfer_t *xfer;
} midi_device_t;

/**
 * @brief USB MIDI host context
 */
typedef struct usb_midi_host_context {
    usb_host_client_handle_t client_handle;
    midi_device_t devices[MAX_MIDI_DEVICES];
    uint8_t device_count;
    bool is_running;
    
    usb_midi_rx_callback_t midi_rx_callback;
    usb_midi_device_connected_callback_t device_connected_callback;
    usb_midi_device_disconnected_callback_t device_disconnected_callback;
    
    SemaphoreHandle_t device_list_mutex;
    TaskHandle_t host_task_handle;
    QueueHandle_t event_queue;
} usb_midi_host_context_t;

/**
 * @brief USB event structure for internal use
 */
typedef struct {
    usb_host_client_event_t event;
    usb_device_handle_t device;
} usb_event_t;

/**
 * @brief USB host library event callback
 */
static void usb_host_lib_callback(usb_host_lib_evt_t evt, void *arg)
{
    if (evt == USB_HOST_LIB_ERROR_EVENT) {
        ESP_LOGE(TAG, "USB Host Library error event");
    }
}

/**
 * @brief USB client event callback
 */
static void usb_host_client_callback(const usb_host_client_event_t *event, 
                                     void *arg)
{
    usb_midi_host_context_t *ctx = (usb_midi_host_context_t *)arg;
    if (!ctx || !ctx->event_queue) {
        return;
    }
    
    usb_event_t usb_event = {
        .event = *event,
        .device = event->dev
    };
    
    xQueueSend(ctx->event_queue, &usb_event, 0);
}

/**
 * @brief Find MIDI IN endpoint from device
 */
static uint8_t usb_midi_find_midi_in_endpoint(usb_device_handle_t device,
                                              uint16_t *max_packet_size)
{
    const usb_config_desc_t *config_desc = usb_host_get_active_config_descriptor(device);
    if (!config_desc) {
        return 0;
    }
    
    uint8_t midi_in_ep = 0;
    int offset = 0;
    
    const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(
        config_desc, 0, 0, &offset);
    
    while (intf_desc) {
        // Check for Audio/MIDI interface class (0x01) and Audio Control (0x01)
        if (intf_desc->bInterfaceClass == 0x01 && 
            (intf_desc->bInterfaceSubClass == 0x03 || 
             intf_desc->bInterfaceSubClass == 0x01)) {
            
            // Find endpoints in this interface
            const usb_ep_desc_t *ep_desc = usb_parse_endpoint_descriptor_by_index(
                config_desc, intf_desc, 0, &offset);
            
            while (ep_desc) {
                // Check for bulk or interrupt IN endpoint
                if ((ep_desc->bEndpointAddress & USB_B_ENDPOINT_DIR_MASK) == 
                    USB_B_ENDPOINT_IN) {
                    
                    midi_in_ep = ep_desc->bEndpointAddress;
                    *max_packet_size = ep_desc->wMaxPacketSize & 0x7FF;
                    
                    ESP_LOGD(TAG, "Found MIDI IN endpoint: 0x%02X, "
                            "max packet size: %d",
                            midi_in_ep, *max_packet_size);
                    return midi_in_ep;
                }
                
                ep_desc = usb_parse_endpoint_descriptor_by_index(
                    config_desc, intf_desc, 0, &offset);
            }
        }
        
        intf_desc = usb_parse_interface_descriptor(
            config_desc, 0, 0, &offset);
    }
    
    return 0;
}

/**
 * @brief Get device string descriptor
 */
static esp_err_t usb_midi_get_string(usb_device_handle_t device,
                                     uint8_t index,
                                     char *str_buf,
                                     size_t str_buf_size)
{
    if (!index) {
        str_buf[0] = '\0';
        return ESP_OK;
    }
    
    usb_transfer_t *xfer = usb_host_transfer_alloc(
        device, 64, USB_ENDPOINT_CONTROL, NULL);
    if (!xfer) {
        return ESP_ERR_NO_MEM;
    }
    
    // Request string descriptor
    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = USB_B_REQUEST_TYPE_DIR_IN |
                          USB_B_REQUEST_TYPE_RECIPIENT_DEVICE;
    setup->bRequest = USB_B_GET_DESCRIPTOR;
    setup->wValue = (USB_W_VALUE_DT_STRING << 8) | index;
    setup->wIndex = 0;
    setup->wLength = 64 - USB_TRANSFER_DATA_MAX_BYTES;
    
    xfer->num_bytes = 64;
    
    esp_err_t err = usb_host_transfer_submit_control(device, xfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(xfer);
        return err;
    }
    
    // Wait for transfer to complete
    while (xfer->status == USB_TRANSFER_STATUS_IN_PROGRESS) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        // Parse string descriptor
        if (xfer->actual_num_bytes >= 2) {
            uint8_t *str_desc = (uint8_t *)xfer->data_buffer;
            uint8_t len = str_desc[0];
            
            if (len > 0 && str_desc[1] == 0x03) {  // String descriptor type
                // Convert UTF-16LE to ASCII
                int str_len = (len - 2) / 2;
                int i = 0;
                
                while (i < str_len && i < (int)str_buf_size - 1) {
                    str_buf[i] = str_desc[2 + i * 2];
                    if (str_buf[i] == 0) break;
                    i++;
                }
                str_buf[i] = '\0';
            }
        }
    }
    
    usb_host_transfer_free(xfer);
    return ESP_OK;
}

/**
 * @brief MIDI data transfer task for a device
 */
static void midi_transfer_task(void *arg)
{
    midi_device_t *device = (midi_device_t *)arg;
    uint8_t midi_data[64];
    
    ESP_LOGI(TAG, "MIDI transfer task started for device %d", device->dev_addr);
    
    // Allocate transfer handle
    device->xfer = usb_host_transfer_alloc(
        device->usb_dev,
        64,
        device->device_info.midi_in_endpoint,
        NULL);
    
    if (!device->xfer) {
        ESP_LOGE(TAG, "Failed to allocate transfer for device %d", 
                device->dev_addr);
        vTaskDelete(NULL);
        return;
    }
    
    while (device->state == MIDI_DEVICE_STATE_ACTIVE) {
        ESP_LOGD(TAG, "Submitting MIDI IN transfer for device %d", 
                device->dev_addr);
        
        device->xfer->data_buffer = midi_data;
        device->xfer->num_bytes = sizeof(midi_data);
        device->xfer->user_ctx = device;
        
        esp_err_t err = usb_host_transfer_submit(device->xfer);
        
        if (err == ESP_OK) {
            // Wait for transfer to complete
            while (device->xfer->status == USB_TRANSFER_STATUS_IN_PROGRESS &&
                   device->state == MIDI_DEVICE_STATE_ACTIVE) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            if (device->xfer->status == USB_TRANSFER_STATUS_COMPLETED &&
                device->xfer->actual_num_bytes > 0) {
                ESP_LOGD(TAG, "Received %d bytes from device %d",
                        device->xfer->actual_num_bytes, device->dev_addr);
                
                // MIDI data received successfully
                // Note: Application should process this data
            }
        } else {
            ESP_LOGW(TAG, "Transfer submit failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (device->xfer) {
        usb_host_transfer_free(device->xfer);
        device->xfer = NULL;
    }
    
    ESP_LOGI(TAG, "MIDI transfer task ended for device %d", device->dev_addr);
    vTaskDelete(NULL);
}

/**
 * @brief Handle new USB device appearance
 */
static esp_err_t handle_new_device(usb_midi_host_context_t *ctx,
                                   usb_device_handle_t device_handle)
{
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Check device limit
    if (ctx->device_count >= MAX_MIDI_DEVICES) {
        ESP_LOGW(TAG, "Maximum MIDI devices reached (%d)", MAX_MIDI_DEVICES);
        xSemaphoreGive(ctx->device_list_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t dev_addr = usb_host_device_addr_list_index_to_addr(0);
    
    // Get device descriptor
    const usb_device_desc_t *device_desc = 
        usb_host_get_device_descriptor(device_handle);
    
    if (!device_desc) {
        ESP_LOGE(TAG, "Failed to get device descriptor");
        xSemaphoreGive(ctx->device_list_mutex);
        return ESP_FAIL;
    }
    
    // Find MIDI IN endpoint
    uint16_t max_packet_size = 0;
    uint8_t midi_in_ep = usb_midi_find_midi_in_endpoint(
        device_handle, &max_packet_size);
    
    if (!midi_in_ep) {
        ESP_LOGW(TAG, "Device does not have MIDI IN endpoint");
        xSemaphoreGive(ctx->device_list_mutex);
        return ESP_FAIL;
    }
    
    // Create device instance
    midi_device_t *device = &ctx->devices[ctx->device_count];
    memset(device, 0, sizeof(midi_device_t));
    
    device->dev_addr = dev_addr;
    device->usb_dev = device_handle;
    device->device_info.vendor_id = device_desc->idVendor;
    device->device_info.product_id = device_desc->idProduct;
    device->device_info.device_address = (uint8_t)dev_addr;
    device->device_info.midi_in_endpoint = midi_in_ep;
    device->device_info.max_packet_size = max_packet_size;
    device->device_info.is_connected = true;
    
    // Get manufacturer and product name
    usb_midi_get_string(device_handle, device_desc->iManufacturer,
                       device->device_info.manufacturer,
                       sizeof(device->device_info.manufacturer));
    
    usb_midi_get_string(device_handle, device_desc->iProduct,
                       device->device_info.product_name,
                       sizeof(device->device_info.product_name));
    
    device->state = MIDI_DEVICE_STATE_CONFIGURED;
    
    uint8_t device_index = ctx->device_count;
    ctx->device_count++;
    
    ESP_LOGI(TAG, "MIDI device connected: %s %s (VID: 0x%04X, PID: 0x%04X)",
            device->device_info.manufacturer,
            device->device_info.product_name,
            device->device_info.vendor_id,
            device->device_info.product_id);
    
    xSemaphoreGive(ctx->device_list_mutex);
    
    // Notify application
    if (ctx->device_connected_callback) {
        ctx->device_connected_callback(device_index, &device->device_info);
    }
    
    // Start MIDI transfer task
    if (xTaskCreate(midi_transfer_task, "midi_transfer", 4096, device, 5, 
                   &device->transfer_task) == pdPASS) {
        device->state = MIDI_DEVICE_STATE_ACTIVE;
    } else {
        ESP_LOGE(TAG, "Failed to create MIDI transfer task");
    }
    
    return ESP_OK;
}

/**
 * @brief USB host event processing task
 */
static void usb_host_task(void *arg)
{
    usb_midi_host_context_t *ctx = (usb_midi_host_context_t *)arg;
    usb_event_t usb_event;
    
    ESP_LOGI(TAG, "USB host task started");
    
    while (ctx->is_running) {
        if (xQueueReceive(ctx->event_queue, &usb_event, pdMS_TO_TICKS(100))) {
            usb_host_client_event_t event = usb_event.event;
            
            switch (event.action) {
                case USB_HOST_CLIENT_EVENT_NEW_DEV:
                    ESP_LOGD(TAG, "USB device new dev event");
                    handle_new_device(ctx, event.dev);
                    break;
                
                case USB_HOST_CLIENT_EVENT_DEV_GONE:
                    ESP_LOGI(TAG, "USB device gone event");
                    if (xSemaphoreTake(ctx->device_list_mutex, 
                                      pdMS_TO_TICKS(1000)) == pdTRUE) {
                        for (uint8_t i = 0; i < ctx->device_count; i++) {
                            if (ctx->devices[i].dev_addr == event.dev_addr) {
                                ctx->devices[i].state = MIDI_DEVICE_STATE_IDLE;
                                
                                if (ctx->device_disconnected_callback) {
                                    ctx->device_disconnected_callback(i);
                                }
                                
                                // Remove device
                                if (i < ctx->device_count - 1) {
                                    memmove(&ctx->devices[i],
                                           &ctx->devices[i + 1],
                                           (ctx->device_count - i - 1) * 
                                           sizeof(midi_device_t));
                                }
                                ctx->device_count--;
                                break;
                            }
                        }
                        xSemaphoreGive(ctx->device_list_mutex);
                    }
                    break;
                
                default:
                    ESP_LOGD(TAG, "USB event action: %d", event.action);
                    break;
            }
        }
        
        // Handle USB library events
        usb_host_lib_handle_events(ctx->client_handle);
    }
    
    ESP_LOGI(TAG, "USB host task ended");
    vTaskDelete(NULL);
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

usb_midi_host_context_t* usb_midi_host_init(const usb_midi_host_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }
    
    usb_midi_host_context_t *ctx = calloc(1, sizeof(usb_midi_host_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    ctx->midi_rx_callback = config->midi_rx_callback;
    ctx->device_connected_callback = config->device_connected_callback;
    ctx->device_disconnected_callback = config->device_disconnected_callback;
    
    // Create mutex for device list protection
    ctx->device_list_mutex = xSemaphoreCreateMutex();
    if (!ctx->device_list_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(ctx);
        return NULL;
    }
    
    // Create event queue
    ctx->event_queue = xQueueCreate(10, sizeof(usb_event_t));
    if (!ctx->event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        vSemaphoreDelete(ctx->device_list_mutex);
        free(ctx);
        return NULL;
    }
    
    ESP_LOGI(TAG, "USB MIDI host initialized successfully");
    return ctx;
}

void usb_midi_host_deinit(usb_midi_host_context_t* ctx)
{
    if (!ctx) return;
    
    if (ctx->is_running) {
        usb_midi_host_stop(ctx);
    }
    
    if (ctx->event_queue) {
        vQueueDelete(ctx->event_queue);
    }
    
    if (ctx->device_list_mutex) {
        vSemaphoreDelete(ctx->device_list_mutex);
    }
    
    free(ctx);
    ESP_LOGI(TAG, "USB MIDI host deinitialized");
}

bool usb_midi_host_start(usb_midi_host_context_t* ctx)
{
    if (!ctx || ctx->is_running) {
        return false;
    }
    
    // Install USB host driver
    usb_host_config_t host_config = {
        .skip_enum_delay_ms = 0,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host driver: %s",
                esp_err_to_name(err));
        return false;
    }
    
    // Register client
    usb_host_client_config_t client_config = {
        .async = {
            .client_event_callback = usb_host_client_callback,
            .callback_arg = ctx,
        },
        .max_num_of_clients = 1,
    };
    
    err = usb_host_client_register(&client_config, &ctx->client_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register client: %s", esp_err_to_name(err));
        usb_host_uninstall();
        return false;
    }
    
    ctx->is_running = true;
    
    // Create USB host event processing task
    if (xTaskCreate(usb_host_task, "usb_midi_host", 4096, ctx, 
                   USB_HOST_PRIORITY, &ctx->host_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create host task");
        usb_host_client_deregister(ctx->client_handle);
        usb_host_uninstall();
        ctx->is_running = false;
        return false;
    }
    
    ESP_LOGI(TAG, "USB MIDI host started");
    return true;
}

void usb_midi_host_stop(usb_midi_host_context_t* ctx)
{
    if (!ctx || !ctx->is_running) {
        return;
    }
    
    ctx->is_running = false;
    
    // Wait for host task to finish
    for (int i = 0; i < 20; i++) {
        if (!ctx->host_task_handle) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Deregister client
    if (ctx->client_handle) {
        usb_host_client_deregister(ctx->client_handle);
        ctx->client_handle = NULL;
    }
    
    // Uninstall USB host
    usb_host_uninstall();
    
    ESP_LOGI(TAG, "USB MIDI host stopped");
}

uint8_t usb_midi_host_get_device_count(usb_midi_host_context_t* ctx)
{
    if (!ctx) return 0;
    
    uint8_t count = 0;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = ctx->device_count;
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    return count;
}

bool usb_midi_host_get_device_info(usb_midi_host_context_t* ctx,
                                   uint8_t device_index,
                                   usb_midi_device_t* device_info)
{
    if (!ctx || !device_info) return false;
    
    bool found = false;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (device_index < ctx->device_count) {
            memcpy(device_info, &ctx->devices[device_index].device_info,
                  sizeof(usb_midi_device_t));
            found = true;
        }
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    return found;
}

bool usb_midi_host_is_device_connected(usb_midi_host_context_t* ctx,
                                       uint8_t device_index)
{
    if (!ctx) return false;
    
    bool connected = false;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (device_index < ctx->device_count) {
            connected = ctx->devices[device_index].device_info.is_connected;
        }
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    return connected;
}

bool usb_midi_host_is_running(usb_midi_host_context_t* ctx)
{
    if (!ctx) return false;
    return ctx->is_running;
}

int usb_midi_host_get_endpoint_count(usb_midi_host_context_t* ctx,
                                     uint8_t device_index)
{
    if (!ctx) return -1;
    
    int count = -1;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (device_index < ctx->device_count) {
            // For MIDI, typically 1 IN endpoint
            count = (ctx->devices[device_index].device_info.midi_in_endpoint != 0) ? 1 : 0;
        }
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    return count;
}
