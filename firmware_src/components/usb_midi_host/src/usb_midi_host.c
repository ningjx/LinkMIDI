/**
 * @file usb_midi_host.c
 * @brief USB MIDI Host Driver Implementation
 * 
 * Based on touchgadget/esp32-usb-host-demos usbhmidi example
 * Reference: https://github.com/touchgadget/esp32-usb-host-demos
 * 
 * USB MIDI Class Specification:
 * - Interface Class: USB_CLASS_AUDIO (0x01)
 * - Interface SubClass: 0x03 (MIDI)
 * - Interface Protocol: 0x00
 * - Transfer Type: BULK
 * 
 * USB MIDI Event Packet Format (4 bytes):
 * | Byte 0 | Byte 1 | Byte 2 | Byte 3 |
 * | CN+CIN | MIDI_0 | MIDI_1 | MIDI_2 |
 * 
 * CN  = Cable Number (0x0-0xF)
 * CIN = Code Index Number (0x0-0xF)
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "usb_midi_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_MIDI_HOST";

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_MIDI_DEVICES        4
#define MIDI_IN_BUFFERS         8       // Number of IN transfer buffers
#define MIDI_OUT_BUFFERS        4       // Number of OUT transfer buffers
#define USB_HOST_TASK_PRIORITY  5
#define USB_HOST_TASK_STACK     4096
#define USB_HOST_EVENT_TIMEOUT  1
#define USB_CLIENT_EVENT_TIMEOUT 1

/* USB MIDI Class identifiers */
#define USB_CLASS_AUDIO          0x01
#define USB_MIDI_SUBCLASS        0x03
#define USB_MIDI_PROTOCOL        0x00

/* USB MIDI CIN (Code Index Number) values */
#define MIDI_CIN_MISC            0x00
#define MIDI_CIN_CABLE_EVENT     0x01
#define MIDI_CIN_SYSCOM_2BYTE    0x02
#define MIDI_CIN_SYSCOM_3BYTE    0x03
#define MIDI_CIN_SYSEX_START     0x04
#define MIDI_CIN_SYSEX_END_1     0x05
#define MIDI_CIN_SYSEX_END_2     0x06
#define MIDI_CIN_SYSEX_END_3     0x07
#define MIDI_CIN_NOTE_OFF        0x08
#define MIDI_CIN_NOTE_ON         0x09
#define MIDI_CIN_POLY_KEYPRESS   0x0A
#define MIDI_CIN_CONTROL_CHANGE  0x0B
#define MIDI_CIN_PROGRAM_CHANGE  0x0C
#define MIDI_CIN_CHANNEL_PRESS   0x0D
#define MIDI_CIN_PITCH_BEND      0x0E
#define MIDI_CIN_SINGLE_BYTE     0x0F

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Internal device state
 */
typedef struct {
    usb_midi_device_t info;
    usb_device_handle_t device_handle;
    
    /* Endpoints */
    uint8_t in_endpoint_addr;
    uint8_t out_endpoint_addr;
    uint16_t in_max_packet_size;
    uint16_t out_max_packet_size;
    
    /* Transfer buffers */
    usb_transfer_t* in_transfers[MIDI_IN_BUFFERS];
    usb_transfer_t* out_transfer;
    
    /* State */
    bool is_claimed;
    bool is_ready;
} midi_device_internal_t;

/**
 * @brief Internal USB MIDI host context
 */
struct usb_midi_host_context {
    /* USB Host handles */
    usb_host_client_handle_t client_handle;
    
    /* Device management */
    midi_device_internal_t devices[MAX_MIDI_DEVICES];
    uint8_t device_count;
    SemaphoreHandle_t device_mutex;
    
    /* Task */
    TaskHandle_t host_task_handle;
    bool is_running;
    bool is_initialized;
    
    /* Callbacks */
    usb_midi_rx_callback_t midi_rx_cb;
    usb_midi_device_connected_callback_t device_connected_cb;
    usb_midi_device_disconnected_callback_t device_disconnected_cb;
};

/* Global context for callbacks */
static usb_midi_host_context_t *g_ctx = NULL;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void usb_host_task(void *arg);
static void usb_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg);
static void midi_in_transfer_callback(usb_transfer_t *transfer);
static void midi_out_transfer_callback(usb_transfer_t *transfer);
static void enumerate_device(usb_device_handle_t dev_hdl);
static bool check_midi_interface(const usb_intf_desc_t *intf);
static void prepare_endpoints(midi_device_internal_t *dev, const usb_ep_desc_t *endpoint);
static void handle_device_gone(usb_device_handle_t dev_hdl);

/* ============================================================================
 * MIDI Packet Helpers
 * ============================================================================ */

/**
 * @brief Get CIN from MIDI status byte
 * Reference: USB MIDI 1.0 Specification, Table 4-1
 */
static uint8_t midi_status_to_cin(uint8_t status) {
    if (status >= 0xF0) {
        // System messages
        switch (status) {
            case 0xF0: return MIDI_CIN_SYSEX_START;
            case 0xF1: return MIDI_CIN_SYSCOM_2BYTE;
            case 0xF2: return MIDI_CIN_SYSCOM_3BYTE;
            case 0xF3: return MIDI_CIN_SYSCOM_3BYTE;
            case 0xF4: 
            case 0xF5: return MIDI_CIN_MISC;
            case 0xF6: return MIDI_CIN_SINGLE_BYTE;
            case 0xF7: return MIDI_CIN_SYSEX_END_1;
            case 0xF8:
            case 0xF9:
            case 0xFA:
            case 0xFB:
            case 0xFC:
            case 0xFD:
            case 0xFE:
            case 0xFF: return MIDI_CIN_SINGLE_BYTE;
            default: return MIDI_CIN_MISC;
        }
    } else {
        // Channel messages
        uint8_t msg_type = status & 0xF0;
        switch (msg_type) {
            case 0x80: return MIDI_CIN_NOTE_OFF;
            case 0x90: return MIDI_CIN_NOTE_ON;
            case 0xA0: return MIDI_CIN_POLY_KEYPRESS;
            case 0xB0: return MIDI_CIN_CONTROL_CHANGE;
            case 0xC0: return MIDI_CIN_PROGRAM_CHANGE;
            case 0xD0: return MIDI_CIN_CHANNEL_PRESS;
            case 0xE0: return MIDI_CIN_PITCH_BEND;
            default: return MIDI_CIN_MISC;
        }
    }
}

/**
 * @brief Extract MIDI bytes from USB MIDI packet
 */
static void parse_midi_packet(const uint8_t *packet, uint8_t *midi_data, uint8_t *midi_len) {
    uint8_t cin = packet[0] & 0x0F;
    *midi_len = 0;
    
    switch (cin) {
        case MIDI_CIN_MISC:
        case MIDI_CIN_CABLE_EVENT:
            // No MIDI data
            break;
        case MIDI_CIN_SYSCOM_2BYTE:
        case MIDI_CIN_SYSEX_END_1:
        case MIDI_CIN_PROGRAM_CHANGE:
        case MIDI_CIN_CHANNEL_PRESS:
        case MIDI_CIN_SINGLE_BYTE:
            midi_data[0] = packet[1];
            *midi_len = 1;
            break;
        case MIDI_CIN_SYSCOM_3BYTE:
        case MIDI_CIN_SYSEX_END_2:
        case MIDI_CIN_NOTE_OFF:
        case MIDI_CIN_NOTE_ON:
        case MIDI_CIN_POLY_KEYPRESS:
        case MIDI_CIN_CONTROL_CHANGE:
        case MIDI_CIN_PITCH_BEND:
            midi_data[0] = packet[1];
            midi_data[1] = packet[2];
            *midi_len = 2;
            break;
        case MIDI_CIN_SYSEX_START:
        case MIDI_CIN_SYSEX_END_3:
            midi_data[0] = packet[1];
            midi_data[1] = packet[2];
            midi_data[2] = packet[3];
            *midi_len = 3;
            break;
    }
}

/* ============================================================================
 * Transfer Callbacks
 * ============================================================================ */

/**
 * @brief MIDI IN transfer callback
 * Reference: touchgadget/esp32-usb-host-demos usbhmidi.ino midi_transfer_cb()
 */
static void midi_in_transfer_callback(usb_transfer_t *transfer) {
    if (!g_ctx) return;
    
    // Find device by handle
    int dev_idx = -1;
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        if (g_ctx->devices[i].device_handle == transfer->device_handle) {
            dev_idx = i;
            break;
        }
    }
    
    if (dev_idx < 0) return;
    
    if (transfer->status == 0 && transfer->actual_num_bytes > 0) {
        uint8_t *p = transfer->data_buffer;
        
        // Process each 4-byte MIDI packet
        for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
            // Check for empty packet (all zeros)
            if ((p[i] + p[i+1] + p[i+2] + p[i+3]) == 0) break;
            
            ESP_LOGD(TAG, "MIDI IN: %02X %02X %02X %02X", p[i], p[i+1], p[i+2], p[i+3]);
            
            // Parse MIDI data
            uint8_t midi_data[3];
            uint8_t midi_len = 0;
            parse_midi_packet(&p[i], midi_data, &midi_len);
            
            // Call user callback
            if (midi_len > 0 && g_ctx->midi_rx_cb) {
                g_ctx->midi_rx_cb(dev_idx, midi_data, midi_len);
            }
        }
        
        // Re-submit transfer for continuous reception
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to re-submit IN transfer: %s", esp_err_to_name(err));
        }
    } else if (transfer->status != 0) {
        ESP_LOGW(TAG, "IN transfer status: %d", transfer->status);
    }
}

/**
 * @brief MIDI OUT transfer callback
 */
static void midi_out_transfer_callback(usb_transfer_t *transfer) {
    if (transfer->status != 0) {
        ESP_LOGW(TAG, "OUT transfer status: %d", transfer->status);
    }
}

/* ============================================================================
 * USB Event Handling
 * ============================================================================ */

/**
 * @brief USB client event callback
 * Reference: touchgadget/esp32-usb-host-demos usbhhelp.hpp _client_event_callback()
 * Reference: ESP-IDF USB Host documentation
 */
static void usb_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg) {
    if (!g_ctx) return;
    
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            ESP_LOGI(TAG, "New USB device connected, address: %d", event_msg->new_dev.address);
            
            // Open device to get handle
            usb_device_handle_t dev_hdl;
            esp_err_t err = usb_host_device_open(g_ctx->client_handle, event_msg->new_dev.address, &dev_hdl);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
                return;
            }
            
            enumerate_device(dev_hdl);
            break;
        }
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "USB device gone");
            handle_device_gone(event_msg->dev_gone.dev_hdl);
            break;
            
        default:
            ESP_LOGD(TAG, "Unknown USB event: %d", event_msg->event);
            break;
    }
}

/**
 * @brief Check if interface is MIDI
 * Reference: touchgadget/esp32-usb-host-demos usbhmidi.ino check_interface_desc_MIDI()
 */
static bool check_midi_interface(const usb_intf_desc_t *intf) {
    return (intf->bInterfaceClass == USB_CLASS_AUDIO &&
            intf->bInterfaceSubClass == USB_MIDI_SUBCLASS &&
            intf->bInterfaceProtocol == USB_MIDI_PROTOCOL);
}

/**
 * @brief Prepare endpoints for MIDI device
 * Reference: touchgadget/esp32-usb-host-demos usbhmidi.ino prepare_endpoints()
 */
static void prepare_endpoints(midi_device_internal_t *dev, const usb_ep_desc_t *endpoint) {
    esp_err_t err;
    
    // Must be BULK for MIDI
    if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
        ESP_LOGW(TAG, "Not bulk endpoint: 0x%02X", endpoint->bmAttributes);
        return;
    }
    
    bool is_in = (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK);
    
    if (is_in) {
        // IN endpoint (device -> host, for receiving MIDI)
        ESP_LOGI(TAG, "MIDI IN endpoint: 0x%02X, max packet: %d", 
                 endpoint->bEndpointAddress, endpoint->wMaxPacketSize);
        
        dev->in_endpoint_addr = endpoint->bEndpointAddress;
        dev->in_max_packet_size = endpoint->wMaxPacketSize;
        
        // Allocate multiple IN transfer buffers for continuous reception
        for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
            err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &dev->in_transfers[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to alloc IN transfer[%d]: %s", i, esp_err_to_name(err));
                dev->in_transfers[i] = NULL;
                continue;
            }
            
            dev->in_transfers[i]->device_handle = dev->device_handle;
            dev->in_transfers[i]->bEndpointAddress = endpoint->bEndpointAddress;
            dev->in_transfers[i]->callback = midi_in_transfer_callback;
            dev->in_transfers[i]->context = (void *)(intptr_t)i;
            dev->in_transfers[i]->num_bytes = endpoint->wMaxPacketSize;
            
            // Submit transfer immediately
            err = usb_host_transfer_submit(dev->in_transfers[i]);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to submit IN transfer[%d]: %s", i, esp_err_to_name(err));
            }
        }
    } else {
        // OUT endpoint (host -> device, for sending MIDI)
        ESP_LOGI(TAG, "MIDI OUT endpoint: 0x%02X, max packet: %d",
                 endpoint->bEndpointAddress, endpoint->wMaxPacketSize);
        
        dev->out_endpoint_addr = endpoint->bEndpointAddress;
        dev->out_max_packet_size = endpoint->wMaxPacketSize;
        
        err = usb_host_transfer_alloc(endpoint->wMaxPacketSize * MIDI_OUT_BUFFERS, 0, &dev->out_transfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to alloc OUT transfer: %s", esp_err_to_name(err));
            dev->out_transfer = NULL;
            return;
        }
        
        dev->out_transfer->device_handle = dev->device_handle;
        dev->out_transfer->bEndpointAddress = endpoint->bEndpointAddress;
        dev->out_transfer->callback = midi_out_transfer_callback;
        dev->out_transfer->context = NULL;
    }
}

/**
 * @brief Enumerate and configure a new USB device
 * Reference: touchgadget/esp32-usb-host-demos usbhhelp.hpp _client_event_callback()
 */
static void enumerate_device(usb_device_handle_t dev_hdl) {
    if (!g_ctx) return;
    
    esp_err_t err;
    
    // Get device descriptor
    const usb_device_desc_t *dev_desc;
    err = usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device descriptor: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "Device: VID=0x%04X PID=0x%04X", dev_desc->idVendor, dev_desc->idProduct);
    
    // Get configuration descriptor
    const usb_config_desc_t *config_desc;
    err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get config descriptor: %s", esp_err_to_name(err));
        return;
    }
    
    // Parse configuration descriptor
    const uint8_t *p = &config_desc->val[0];
    uint8_t bLength;
    bool is_midi = false;
    int free_slot = -1;
    
    // Find free slot
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        if (!g_ctx->devices[i].is_claimed) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot < 0) {
        ESP_LOGW(TAG, "No free device slots");
        return;
    }
    
    midi_device_internal_t *dev = &g_ctx->devices[free_slot];
    memset(dev, 0, sizeof(midi_device_internal_t));
    dev->device_handle = dev_hdl;
    
    // Walk through descriptors
    for (int i = 0; i < config_desc->wTotalLength; i += bLength, p += bLength) {
        bLength = *p;
        if ((i + bLength) > config_desc->wTotalLength) break;
        
        uint8_t bDescriptorType = *(p + 1);
        
        switch (bDescriptorType) {
            case USB_B_DESCRIPTOR_TYPE_INTERFACE: {
                const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
                
                if (!is_midi && check_midi_interface(intf)) {
                    is_midi = true;
                    ESP_LOGI(TAG, "Found MIDI interface %d", intf->bInterfaceNumber);
                    
                    // Claim the interface
                    err = usb_host_interface_claim(g_ctx->client_handle, dev_hdl,
                                                   intf->bInterfaceNumber, intf->bAlternateSetting);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to claim interface: %s", esp_err_to_name(err));
                        is_midi = false;
                    } else {
                        dev->is_claimed = true;
                        dev->info.vendor_id = dev_desc->idVendor;
                        dev->info.product_id = dev_desc->idProduct;
                        dev->info.device_address = dev_desc->bNumConfigurations;
                    }
                }
                break;
            }
            
            case USB_B_DESCRIPTOR_TYPE_ENDPOINT: {
                if (is_midi) {
                    prepare_endpoints(dev, (const usb_ep_desc_t *)p);
                }
                break;
            }
        }
    }
    
    // Check if device is ready
    if (is_midi && dev->in_transfers[0] != NULL) {
        dev->is_ready = true;
        dev->info.is_connected = true;
        g_ctx->device_count++;
        
        ESP_LOGI(TAG, "MIDI device ready at slot %d", free_slot);
        
        // Notify user
        if (g_ctx->device_connected_cb) {
            g_ctx->device_connected_cb(free_slot, &dev->info);
        }
    } else if (is_midi) {
        ESP_LOGW(TAG, "MIDI device not fully configured");
    }
}

/**
 * @brief Handle device disconnection
 */
static void handle_device_gone(usb_device_handle_t dev_hdl) {
    if (!g_ctx) return;
    
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        if (g_ctx->devices[i].device_handle == dev_hdl) {
            ESP_LOGI(TAG, "MIDI device %d disconnected", i);
            
            // Free transfers
            for (int j = 0; j < MIDI_IN_BUFFERS; j++) {
                if (g_ctx->devices[i].in_transfers[j]) {
                    usb_host_transfer_free(g_ctx->devices[i].in_transfers[j]);
                    g_ctx->devices[i].in_transfers[j] = NULL;
                }
            }
            
            if (g_ctx->devices[i].out_transfer) {
                usb_host_transfer_free(g_ctx->devices[i].out_transfer);
                g_ctx->devices[i].out_transfer = NULL;
            }
            
            // Notify user
            if (g_ctx->device_disconnected_cb) {
                g_ctx->device_disconnected_cb(i);
            }
            
            memset(&g_ctx->devices[i], 0, sizeof(midi_device_internal_t));
            g_ctx->device_count--;
            break;
        }
    }
}

/**
 * @brief USB Host background task
 * Reference: touchgadget/esp32-usb-host-demos usbhhelp.hpp usbh_task()
 */
static void usb_host_task(void *arg) {
    ESP_LOGI(TAG, "USB Host task started");
    
    while (g_ctx && g_ctx->is_running) {
        uint32_t event_flags;
        
        // Handle library events
        esp_err_t err = usb_host_lib_handle_events(USB_HOST_EVENT_TIMEOUT, &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                ESP_LOGD(TAG, "No more clients");
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                ESP_LOGD(TAG, "All devices freed");
            }
        }
        
        // Handle client events
        err = usb_host_client_handle_events(g_ctx->client_handle, USB_CLIENT_EVENT_TIMEOUT);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Client event error: %s", esp_err_to_name(err));
        }
    }
    
    ESP_LOGI(TAG, "USB Host task stopped");
    vTaskDelete(NULL);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

usb_midi_host_context_t* usb_midi_host_init(const usb_midi_host_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return NULL;
    }
    
    if (g_ctx) {
        ESP_LOGW(TAG, "USB MIDI Host already initialized");
        return g_ctx;
    }
    
    usb_midi_host_context_t *ctx = calloc(1, sizeof(usb_midi_host_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    // Create mutex
    ctx->device_mutex = xSemaphoreCreateMutex();
    if (!ctx->device_mutex) {
        free(ctx);
        ESP_LOGE(TAG, "Failed to create mutex");
        return NULL;
    }
    
    // Save callbacks
    ctx->midi_rx_cb = config->midi_rx_callback;
    ctx->device_connected_cb = config->device_connected_callback;
    ctx->device_disconnected_cb = config->device_disconnected_callback;
    
    // Install USB host driver
    usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        vSemaphoreDelete(ctx->device_mutex);
        free(ctx);
        return NULL;
    }
    
    // Register USB client
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_client_event_callback,
            .callback_arg = ctx,
        },
    };
    
    err = usb_host_client_register(&client_config, &ctx->client_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(err));
        usb_host_uninstall();
        vSemaphoreDelete(ctx->device_mutex);
        free(ctx);
        return NULL;
    }
    
    ctx->is_initialized = true;
    g_ctx = ctx;
    
    ESP_LOGI(TAG, "USB MIDI Host initialized");
    return ctx;
}

void usb_midi_host_deinit(usb_midi_host_context_t* ctx) {
    if (!ctx || ctx != g_ctx) return;
    
    ctx->is_running = false;
    
    // Wait for task to finish
    if (ctx->host_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Free all transfers
    for (int i = 0; i < MAX_MIDI_DEVICES; i++) {
        for (int j = 0; j < MIDI_IN_BUFFERS; j++) {
            if (ctx->devices[i].in_transfers[j]) {
                usb_host_transfer_free(ctx->devices[i].in_transfers[j]);
            }
        }
        if (ctx->devices[i].out_transfer) {
            usb_host_transfer_free(ctx->devices[i].out_transfer);
        }
    }
    
    if (ctx->client_handle) {
        usb_host_client_deregister(ctx->client_handle);
    }
    
    usb_host_uninstall();
    
    if (ctx->device_mutex) {
        vSemaphoreDelete(ctx->device_mutex);
    }
    
    free(ctx);
    g_ctx = NULL;
    
    ESP_LOGI(TAG, "USB MIDI Host deinitialized");
}

bool usb_midi_host_start(usb_midi_host_context_t* ctx) {
    if (!ctx || ctx != g_ctx) {
        return false;
    }
    
    if (ctx->is_running) {
        return true;
    }
    
    ctx->is_running = true;
    
    // Create USB host task
    BaseType_t ret = xTaskCreate(
        usb_host_task,
        "usb_midi_host",
        USB_HOST_TASK_STACK,
        NULL,
        USB_HOST_TASK_PRIORITY,
        &ctx->host_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB host task");
        ctx->is_running = false;
        return false;
    }
    
    ESP_LOGI(TAG, "USB MIDI Host started");
    return true;
}

void usb_midi_host_stop(usb_midi_host_context_t* ctx) {
    if (!ctx || ctx != g_ctx) return;
    
    ctx->is_running = false;
    ESP_LOGI(TAG, "USB MIDI Host stopped");
}

uint8_t usb_midi_host_get_device_count(usb_midi_host_context_t* ctx) {
    if (!ctx) return 0;
    return ctx->device_count;
}

bool usb_midi_host_get_device_info(usb_midi_host_context_t* ctx,
                                   uint8_t device_index,
                                   usb_midi_device_t* device_info) {
    if (!ctx || !device_info || device_index >= MAX_MIDI_DEVICES) {
        return false;
    }
    
    if (ctx->devices[device_index].is_ready) {
        memcpy(device_info, &ctx->devices[device_index].info, sizeof(usb_midi_device_t));
        return true;
    }
    
    return false;
}

bool usb_midi_host_is_device_connected(usb_midi_host_context_t* ctx,
                                       uint8_t device_index) {
    if (!ctx || device_index >= MAX_MIDI_DEVICES) {
        return false;
    }
    return ctx->devices[device_index].is_ready;
}

bool usb_midi_host_is_running(usb_midi_host_context_t* ctx) {
    if (!ctx) return false;
    return ctx->is_running;
}

int usb_midi_host_get_endpoint_count(usb_midi_host_context_t* ctx,
                                     uint8_t device_index) {
    if (!ctx || device_index >= MAX_MIDI_DEVICES) {
        return -1;
    }
    
    if (ctx->devices[device_index].is_ready) {
        int count = 0;
        if (ctx->devices[device_index].in_endpoint_addr) count++;
        if (ctx->devices[device_index].out_endpoint_addr) count++;
        return count;
    }
    return 0;
}

/* ============================================================================
 * MIDI Send Functions
 * ============================================================================ */

bool usb_midi_host_send(usb_midi_host_context_t* ctx,
                        uint8_t device_index,
                        const uint8_t* data,
                        uint16_t length) {
    if (!ctx || !data || length == 0 || device_index >= MAX_MIDI_DEVICES) {
        return false;
    }
    
    midi_device_internal_t *dev = &ctx->devices[device_index];
    if (!dev->is_ready || !dev->out_transfer) {
        ESP_LOGW(TAG, "Device %d not ready for sending", device_index);
        return false;
    }
    
    // Build USB MIDI packet
    uint8_t packet[4] = {0};
    uint8_t cable_num = 0;  // Use cable 0
    
    if (length >= 1) {
        uint8_t status = data[0];
        uint8_t cin = midi_status_to_cin(status);
        packet[0] = (cable_num << 4) | cin;
        packet[1] = data[0];
        
        if (length >= 2) packet[2] = data[1];
        if (length >= 3) packet[3] = data[2];
    }
    
    // Copy to transfer buffer
    memcpy(dev->out_transfer->data_buffer, packet, 4);
    dev->out_transfer->num_bytes = 4;
    
    // Submit transfer
    esp_err_t err = usb_host_transfer_submit(dev->out_transfer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to submit OUT transfer: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGD(TAG, "Sent MIDI: %02X %02X %02X %02X", packet[0], packet[1], packet[2], packet[3]);
    return true;
}

bool usb_midi_host_send_note_on(usb_midi_host_context_t* ctx,
                                uint8_t device_index,
                                uint8_t note,
                                uint8_t velocity,
                                uint8_t channel) {
    uint8_t data[3];
    data[0] = 0x90 | (channel & 0x0F);
    data[1] = note & 0x7F;
    data[2] = velocity & 0x7F;
    return usb_midi_host_send(ctx, device_index, data, 3);
}

bool usb_midi_host_send_note_off(usb_midi_host_context_t* ctx,
                                 uint8_t device_index,
                                 uint8_t note,
                                 uint8_t velocity,
                                 uint8_t channel) {
    uint8_t data[3];
    data[0] = 0x80 | (channel & 0x0F);
    data[1] = note & 0x7F;
    data[2] = velocity & 0x7F;
    return usb_midi_host_send(ctx, device_index, data, 3);
}

bool usb_midi_host_send_control_change(usb_midi_host_context_t* ctx,
                                       uint8_t device_index,
                                       uint8_t controller,
                                       uint8_t value,
                                       uint8_t channel) {
    uint8_t data[3];
    data[0] = 0xB0 | (channel & 0x0F);
    data[1] = controller & 0x7F;
    data[2] = value & 0x7F;
    return usb_midi_host_send(ctx, device_index, data, 3);
}
