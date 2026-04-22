#pragma once

#include "bambu_monitor_config.h"

#include <furi.h>
#include <furi_hal.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct BambuTransport BambuTransport;

typedef struct {
    char ssid[BAMBU_MONITOR_WIFI_SSID_SIZE];
    int16_t rssi;
    bool secure;
} BambuWifiNetworkInfo;

typedef struct {
    char serial[BAMBU_MONITOR_SERIAL_SIZE];
    char name[BAMBU_MONITOR_NAME_SIZE];
    char model[BAMBU_MONITOR_MODEL_SIZE];
    char ip[BAMBU_MONITOR_IP_SIZE];
    char access_code[BAMBU_MONITOR_ACCESS_CODE_SIZE];
    char cloud_status[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    char state[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    char wifi_signal[BAMBU_MONITOR_WIFI_SIGNAL_SIZE];
    char file_name[BAMBU_MONITOR_FILE_SIZE];
    bool online;
    bool has_status;
    bool has_progress;
    bool has_layer;
    bool has_total_layers;
    bool has_remaining_minutes;
    bool has_speed;
    bool has_fan;
    bool has_fan_aux1;
    bool has_fan_aux2;
    bool has_nozzle_temp;
    bool has_bed_temp;
    uint8_t progress;
    uint16_t layer;
    uint16_t total_layers;
    uint16_t remaining_minutes;
    uint16_t speed;
    uint16_t fan;
    uint16_t fan_aux1;
    uint16_t fan_aux2;
    float nozzle_temp;
    float bed_temp;
} BambuPrinterInfo;

typedef struct {
    const char* name;
    bool (*init)(BambuTransport* transport);
    void (*deinit)(BambuTransport* transport);
    bool (*ping_bridge)(BambuTransport* transport);
    bool (*scan_wifi)(BambuTransport* transport);
    bool (*wifi_connect)(BambuTransport* transport, const char* ssid, const char* password);
    bool (*wifi_reconnect)(BambuTransport* transport);
    bool (*wifi_status)(BambuTransport* transport);
    bool (*set_token)(BambuTransport* transport, const char* token);
    bool (*test_cloud_profile)(BambuTransport* transport);
    bool (*discover_printers)(BambuTransport* transport);
    bool (*refresh_status)(BambuTransport* transport, const char* serial);
    bool (*is_ready)(const BambuTransport* transport);
} BambuTransportOps;

typedef void (*BambuTransportActivityCallback)(void* context, bool active);
typedef void (*BambuTransportProgressCallback)(void* context);

struct BambuTransport {
    const BambuTransportOps* ops;
    bool initialized;
    bool bridge_ready;
    char wifi_status[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    char wifi_ssid[BAMBU_MONITOR_WIFI_SSID_SIZE];
    char wifi_ip[BAMBU_MONITOR_IP_SIZE];
    char last_response[BAMBU_MONITOR_DETAIL_TEXT_SIZE];
    BambuWifiNetworkInfo wifi_networks[BAMBU_MONITOR_MAX_WIFI_NETWORKS];
    size_t wifi_network_count;
    BambuPrinterInfo printers[BAMBU_MONITOR_MAX_PRINTERS];
    size_t printer_count;
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    char tx_line_buffer[512];
    bool busy_has_progress;
    uint16_t busy_current;
    uint16_t busy_total;
    char busy_label[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    BambuTransportActivityCallback activity_callback;
    void* activity_context;
    BambuTransportProgressCallback progress_callback;
    void* progress_context;
};

const BambuTransportOps* bambu_transport_live_ops(void);
const char* bambu_transport_name(void);

static inline bool bambu_transport_init(BambuTransport* transport, const BambuTransportOps* ops) {
    if(!transport || !ops || !ops->init) {
        return false;
    }

    memset(transport, 0, sizeof(BambuTransport));
    transport->ops = ops;
    return ops->init(transport);
}

static inline void bambu_transport_deinit(BambuTransport* transport) {
    if(transport && transport->ops && transport->ops->deinit) {
        transport->ops->deinit(transport);
    }
}

static inline bool bambu_transport_ping_bridge(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->ping_bridge &&
           transport->ops->ping_bridge(transport);
}

static inline bool bambu_transport_scan_wifi(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->scan_wifi &&
           transport->ops->scan_wifi(transport);
}

static inline bool bambu_transport_wifi_connect(
    BambuTransport* transport,
    const char* ssid,
    const char* password) {
    return transport && transport->ops && transport->ops->wifi_connect &&
           transport->ops->wifi_connect(transport, ssid, password);
}

static inline bool bambu_transport_wifi_reconnect(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->wifi_reconnect &&
           transport->ops->wifi_reconnect(transport);
}

static inline bool bambu_transport_wifi_status(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->wifi_status &&
           transport->ops->wifi_status(transport);
}

static inline bool bambu_transport_set_token(BambuTransport* transport, const char* token) {
    return transport && transport->ops && transport->ops->set_token &&
           transport->ops->set_token(transport, token);
}

static inline bool bambu_transport_test_cloud_profile(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->test_cloud_profile &&
           transport->ops->test_cloud_profile(transport);
}

static inline bool bambu_transport_discover_printers(BambuTransport* transport) {
    return transport && transport->ops && transport->ops->discover_printers &&
           transport->ops->discover_printers(transport);
}

static inline bool bambu_transport_refresh_status(BambuTransport* transport, const char* serial) {
    return transport && transport->ops && transport->ops->refresh_status &&
           transport->ops->refresh_status(transport, serial);
}

static inline bool bambu_transport_is_ready(const BambuTransport* transport) {
    return transport && transport->ops && transport->ops->is_ready &&
           transport->ops->is_ready(transport);
}

static inline void bambu_transport_set_activity_callback(
    BambuTransport* transport,
    BambuTransportActivityCallback callback,
    void* context) {
    if(!transport) {
        return;
    }

    transport->activity_callback = callback;
    transport->activity_context = context;
}

static inline void bambu_transport_set_progress_callback(
    BambuTransport* transport,
    BambuTransportProgressCallback callback,
    void* context) {
    if(!transport) {
        return;
    }

    transport->progress_callback = callback;
    transport->progress_context = context;
}
