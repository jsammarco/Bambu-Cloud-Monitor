#include "bambu_transport.h"

#include <stdio.h>
#include <string.h>

#define BAMBU_BRIDGE_BAUDRATE 115200U
#define BAMBU_BRIDGE_RX_STREAM_SIZE 1024U
#define BAMBU_BRIDGE_LINE_SIZE 256U
#define BAMBU_BRIDGE_DEFAULT_TIMEOUT_MS 15000U
#define BAMBU_BRIDGE_PING_TIMEOUT_MS 1000U

typedef void (*BambuBridgeLineHandler)(BambuTransport* transport, const char* line);

static uint32_t bambu_bridge_ms_to_ticks(uint32_t ms) {
    uint32_t tick_hz = furi_kernel_get_tick_frequency();
    uint64_t ticks = ((uint64_t)ms * tick_hz + 999ULL) / 1000ULL;
    if(ticks == 0) {
        ticks = 1;
    }
    return (uint32_t)ticks;
}

static void bambu_bridge_set_response(BambuTransport* transport, const char* message) {
    if(!transport) {
        return;
    }

    snprintf(
        transport->last_response,
        sizeof(transport->last_response),
        "%s",
        message ? message : "");
}

static void bambu_bridge_async_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    BambuTransport* transport = context;

    if(!transport || !transport->rx_stream || !handle) {
        return;
    }

    if((event & FuriHalSerialRxEventData) == 0) {
        return;
    }

    while(furi_hal_serial_async_rx_available(handle)) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(transport->rx_stream, &byte, sizeof(byte), 0);
    }
}

static void bambu_bridge_flush_rx(BambuTransport* transport) {
    uint8_t sink[32];

    if(!transport || !transport->rx_stream) {
        return;
    }

    while(furi_stream_buffer_receive(transport->rx_stream, sink, sizeof(sink), 0) > 0) {
    }
}

static bool bambu_bridge_read_line(
    BambuTransport* transport,
    char* line,
    size_t line_size,
    uint32_t timeout_ms) {
    uint8_t byte = 0;
    size_t line_len = 0;
    uint32_t deadline = furi_get_tick() + bambu_bridge_ms_to_ticks(timeout_ms);

    if(!transport || !transport->rx_stream || !line || line_size < 2) {
        return false;
    }

    memset(line, 0, line_size);

    while(furi_get_tick() < deadline) {
        uint32_t now = furi_get_tick();
        uint32_t remaining_ticks = (deadline > now) ? (deadline - now) : 0;
        uint32_t wait_ticks = remaining_ticks > 10U ? 10U : remaining_ticks;
        size_t received =
            furi_stream_buffer_receive(transport->rx_stream, &byte, sizeof(byte), wait_ticks);

        if(received == 0) {
            continue;
        }

        if(byte == '\r') {
            continue;
        }

        if(byte == '\n') {
            if(line_len > 0) {
                line[line_len] = '\0';
                return true;
            }
            continue;
        }

        if(line_len + 1U < line_size) {
            line[line_len++] = (char)byte;
        }
    }

    return false;
}

static void bambu_bridge_send_command(BambuTransport* transport, const char* command) {
    size_t command_len;

    if(!transport || !transport->serial_handle || !command) {
        return;
    }

    snprintf(transport->tx_line_buffer, sizeof(transport->tx_line_buffer), "%s\n", command);
    command_len = strlen(transport->tx_line_buffer);
    furi_hal_serial_tx(
        transport->serial_handle,
        (const uint8_t*)transport->tx_line_buffer,
        command_len);
    furi_hal_serial_tx_wait_complete(transport->serial_handle);
}

static BambuPrinterInfo* bambu_bridge_find_or_add_printer(BambuTransport* transport, const char* serial) {
    size_t index;

    if(!transport || !serial || !serial[0]) {
        return NULL;
    }

    for(index = 0; index < transport->printer_count; index++) {
        if(strcmp(transport->printers[index].serial, serial) == 0) {
            return &transport->printers[index];
        }
    }

    if(transport->printer_count >= BAMBU_MONITOR_MAX_PRINTERS) {
        return NULL;
    }

    index = transport->printer_count++;
    memset(&transport->printers[index], 0, sizeof(BambuPrinterInfo));
    snprintf(transport->printers[index].serial, sizeof(transport->printers[index].serial), "%s", serial);
    return &transport->printers[index];
}

static void bambu_bridge_handle_wifi_line(BambuTransport* transport, const char* line) {
    char ssid[34];
    int rssi = 0;
    char encryption[16];
    char summary[BAMBU_MONITOR_WIFI_ENTRY_SIZE];

    if(!transport || !line || strncmp(line, "WIFI|", 5) != 0) {
        return;
    }

    memset(ssid, 0, sizeof(ssid));
    memset(encryption, 0, sizeof(encryption));
    if(sscanf(line, "WIFI|%33[^|]|%d|%15s", ssid, &rssi, encryption) >= 2) {
        snprintf(summary, sizeof(summary), "%.24s %ddBm", ssid, rssi);
        if(transport->wifi_network_count < BAMBU_MONITOR_MAX_WIFI_NETWORKS) {
            snprintf(
                transport->wifi_networks[transport->wifi_network_count],
                BAMBU_MONITOR_WIFI_ENTRY_SIZE,
                "%s",
                summary);
            transport->wifi_network_count++;
        }
        bambu_bridge_set_response(transport, summary);
    }
}

static void bambu_bridge_handle_printer_line(BambuTransport* transport, const char* line) {
    char serial[BAMBU_MONITOR_SERIAL_SIZE];
    char name[BAMBU_MONITOR_NAME_SIZE];
    char model[BAMBU_MONITOR_MODEL_SIZE];
    char ip[BAMBU_MONITOR_IP_SIZE];
    char access_code[BAMBU_MONITOR_ACCESS_CODE_SIZE];
    char online[8];
    char cloud_status[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    BambuPrinterInfo* printer;

    if(!transport || !line || strncmp(line, "PRINTER|", 8) != 0) {
        return;
    }

    memset(serial, 0, sizeof(serial));
    memset(name, 0, sizeof(name));
    memset(model, 0, sizeof(model));
    memset(ip, 0, sizeof(ip));
    memset(access_code, 0, sizeof(access_code));
    memset(online, 0, sizeof(online));
    memset(cloud_status, 0, sizeof(cloud_status));

    if(sscanf(
           line,
           "PRINTER|%31[^|]|%47[^|]|%23[^|]|%15[^|]|%31[^|]|%7[^|]|%63[^|]",
           serial,
           name,
           model,
           ip,
           access_code,
           online,
           cloud_status) != 7) {
        return;
    }

    printer = bambu_bridge_find_or_add_printer(transport, serial);
    if(!printer) {
        return;
    }

    snprintf(printer->name, sizeof(printer->name), "%s", name);
    snprintf(printer->model, sizeof(printer->model), "%s", model);
    snprintf(printer->ip, sizeof(printer->ip), "%s", ip);
    snprintf(printer->access_code, sizeof(printer->access_code), "%s", access_code);
    snprintf(printer->cloud_status, sizeof(printer->cloud_status), "%s", cloud_status);
    printer->online = strcmp(online, "1") == 0;

    bambu_bridge_set_response(transport, printer->name);
}

static void bambu_bridge_handle_status_line(BambuTransport* transport, const char* line) {
    char serial[BAMBU_MONITOR_SERIAL_SIZE];
    char state[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    unsigned int progress = 0;
    unsigned int layer = 0;
    unsigned int total_layers = 0;
    unsigned int remaining = 0;
    float nozzle = 0.0f;
    float bed = 0.0f;
    char wifi[BAMBU_MONITOR_WIFI_SIGNAL_SIZE];
    char file_name[BAMBU_MONITOR_FILE_SIZE];
    BambuPrinterInfo* printer;

    if(!transport || !line || strncmp(line, "STATUS|", 7) != 0) {
        return;
    }

    memset(serial, 0, sizeof(serial));
    memset(state, 0, sizeof(state));
    memset(wifi, 0, sizeof(wifi));
    memset(file_name, 0, sizeof(file_name));

    if(sscanf(
           line,
           "STATUS|%31[^|]|%63[^|]|%u|%u|%u|%u|%f|%f|%15[^|]|%47[^|]",
           serial,
           state,
           &progress,
           &layer,
           &total_layers,
           &remaining,
           &nozzle,
           &bed,
           wifi,
           file_name) != 10) {
        return;
    }

    printer = bambu_bridge_find_or_add_printer(transport, serial);
    if(!printer) {
        return;
    }

    snprintf(printer->state, sizeof(printer->state), "%s", state);
    snprintf(printer->wifi_signal, sizeof(printer->wifi_signal), "%s", wifi);
    snprintf(printer->file_name, sizeof(printer->file_name), "%s", file_name);
    printer->progress = (uint8_t)progress;
    printer->layer = (uint16_t)layer;
    printer->total_layers = (uint16_t)total_layers;
    printer->remaining_minutes = (uint16_t)remaining;
    printer->nozzle_temp = nozzle;
    printer->bed_temp = bed;
    printer->has_status = true;

    bambu_bridge_set_response(transport, printer->state);
}

static bool bambu_bridge_wait_for_result(
    BambuTransport* transport,
    uint32_t timeout_ms,
    BambuBridgeLineHandler line_handler) {
    char line[BAMBU_BRIDGE_LINE_SIZE];

    while(bambu_bridge_read_line(transport, line, sizeof(line), timeout_ms)) {
        if(strncmp(line, "OK|", 3) == 0) {
            if(strcmp(line, "OK|PONG") == 0) {
                bambu_bridge_set_response(transport, "Bridge online");
            } else if(strncmp(line, "OK|", 3) == 0 && transport->last_response[0] == '\0') {
                bambu_bridge_set_response(transport, line + 3);
            }
            return true;
        }

        if(strncmp(line, "ERR|", 4) == 0) {
            bambu_bridge_set_response(transport, line + 4);
            return false;
        }

        if(line_handler) {
            line_handler(transport, line);
        }
    }

    bambu_bridge_set_response(transport, "Bridge timeout");
    return false;
}

static bool bambu_bridge_ping(BambuTransport* transport) {
    bambu_bridge_set_response(transport, "");
    bambu_bridge_flush_rx(transport);
    bambu_bridge_send_command(transport, "PING");
    return bambu_bridge_wait_for_result(transport, BAMBU_BRIDGE_PING_TIMEOUT_MS, NULL);
}

static bool bambu_bridge_ensure_ready(BambuTransport* transport) {
    if(!transport || !transport->initialized || !transport->serial_handle) {
        return false;
    }

    if(transport->bridge_ready) {
        return true;
    }

    transport->bridge_ready = bambu_bridge_ping(transport);
    return transport->bridge_ready;
}

static bool bambu_transport_live_init(BambuTransport* transport) {
    if(!transport) {
        return false;
    }

    transport->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!transport->serial_handle) {
        bambu_bridge_set_response(transport, "USART busy");
        return false;
    }

    transport->rx_stream = furi_stream_buffer_alloc(BAMBU_BRIDGE_RX_STREAM_SIZE, 1);
    if(!transport->rx_stream) {
        furi_hal_serial_control_release(transport->serial_handle);
        transport->serial_handle = NULL;
        bambu_bridge_set_response(transport, "RX alloc failed");
        return false;
    }

    furi_hal_serial_init(transport->serial_handle, BAMBU_BRIDGE_BAUDRATE);
    furi_hal_serial_configure_framing(
        transport->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_enable_direction(transport->serial_handle, FuriHalSerialDirectionTx);
    furi_hal_serial_enable_direction(transport->serial_handle, FuriHalSerialDirectionRx);
    furi_hal_serial_async_rx_start(
        transport->serial_handle,
        bambu_bridge_async_rx_callback,
        transport,
        false);

    transport->initialized = true;
    transport->bridge_ready = bambu_bridge_ping(transport);
    return true;
}

static void bambu_transport_live_deinit(BambuTransport* transport) {
    if(!transport) {
        return;
    }

    if(transport->initialized && transport->serial_handle) {
        furi_hal_serial_async_rx_stop(transport->serial_handle);
        furi_hal_serial_disable_direction(transport->serial_handle, FuriHalSerialDirectionTx);
        furi_hal_serial_disable_direction(transport->serial_handle, FuriHalSerialDirectionRx);
        furi_hal_serial_deinit(transport->serial_handle);
        furi_hal_serial_control_release(transport->serial_handle);
    }

    if(transport->rx_stream) {
        furi_stream_buffer_free(transport->rx_stream);
    }

    transport->rx_stream = NULL;
    transport->serial_handle = NULL;
    transport->initialized = false;
    transport->bridge_ready = false;
}

static bool bambu_transport_live_ping_bridge(BambuTransport* transport) {
    if(!transport || !transport->initialized) {
        return false;
    }

    transport->bridge_ready = false;
    transport->bridge_ready = bambu_bridge_ping(transport);
    return transport->bridge_ready;
}

static bool bambu_transport_live_scan_wifi(BambuTransport* transport) {
    if(!transport || !transport->initialized || !bambu_bridge_ensure_ready(transport)) {
        return false;
    }

    transport->wifi_network_count = 0;
    bambu_bridge_set_response(transport, "");
    bambu_bridge_flush_rx(transport);
    bambu_bridge_send_command(transport, "WIFI_SCAN");
    return bambu_bridge_wait_for_result(
        transport,
        BAMBU_BRIDGE_DEFAULT_TIMEOUT_MS,
        bambu_bridge_handle_wifi_line);
}

static bool bambu_transport_live_discover_printers(BambuTransport* transport) {
    if(!transport || !transport->initialized || !bambu_bridge_ensure_ready(transport)) {
        return false;
    }

    transport->printer_count = 0;
    memset(transport->printers, 0, sizeof(transport->printers));
    bambu_bridge_set_response(transport, "");
    bambu_bridge_flush_rx(transport);
    bambu_bridge_send_command(transport, "BAMBU_DISCOVER");
    return bambu_bridge_wait_for_result(
        transport,
        BAMBU_BRIDGE_DEFAULT_TIMEOUT_MS,
        bambu_bridge_handle_printer_line);
}

static bool bambu_transport_live_refresh_status(BambuTransport* transport, const char* serial) {
    if(!transport || !transport->initialized || !serial || !serial[0] ||
       !bambu_bridge_ensure_ready(transport)) {
        return false;
    }

    bambu_bridge_set_response(transport, "");
    bambu_bridge_flush_rx(transport);
    snprintf(transport->tx_line_buffer, sizeof(transport->tx_line_buffer), "BAMBU_STATUS|%s", serial);
    bambu_bridge_send_command(transport, transport->tx_line_buffer);
    return bambu_bridge_wait_for_result(
        transport,
        BAMBU_BRIDGE_DEFAULT_TIMEOUT_MS,
        bambu_bridge_handle_status_line);
}

static bool bambu_transport_live_is_ready(const BambuTransport* transport) {
    return transport && transport->initialized && transport->bridge_ready;
}

static const BambuTransportOps bambu_transport_live_instance = {
    .name = "uart-bridge",
    .init = bambu_transport_live_init,
    .deinit = bambu_transport_live_deinit,
    .ping_bridge = bambu_transport_live_ping_bridge,
    .scan_wifi = bambu_transport_live_scan_wifi,
    .discover_printers = bambu_transport_live_discover_printers,
    .refresh_status = bambu_transport_live_refresh_status,
    .is_ready = bambu_transport_live_is_ready,
};

const BambuTransportOps* bambu_transport_live_ops(void) {
    return &bambu_transport_live_instance;
}

const char* bambu_transport_name(void) {
    return "uart-bridge";
}
