#include "bambu_monitor_app.h"

#include "app_ui.h"

#include <notification/notification_messages.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

typedef enum {
    BambuMonitorViewMain = 0,
    BambuMonitorViewKeyboard,
} BambuMonitorViewId;

typedef enum {
    BambuMonitorCustomEventPingBridge = 1,
    BambuMonitorCustomEventScanWifi,
    BambuMonitorCustomEventWifiReconnect,
    BambuMonitorCustomEventWifiConnectSelected,
    BambuMonitorCustomEventTestCloudApi,
    BambuMonitorCustomEventDiscover,
    BambuMonitorCustomEventRefreshSelected,
    BambuMonitorCustomEventLoadTokenFromSd,
    BambuMonitorCustomEventLoadPrintersFromSd,
} BambuMonitorCustomEvent;

typedef struct {
    BambuMonitorApp* app;
} BambuMonitorMainViewModel;

static void bambu_monitor_app_queue_busy_action(
    BambuMonitorApp* app,
    uint32_t event,
    const char* status,
    const char* detail);
static bool bambu_monitor_app_load_printer_cache_from_path(BambuMonitorApp* app, const char* path);
static bool bambu_monitor_app_load_printer_cache(BambuMonitorApp* app);
static bool bambu_monitor_app_load_token_from_sd(BambuMonitorApp* app);
static bool bambu_monitor_app_load_token_from_path(BambuMonitorApp* app, const char* path);
static bool bambu_monitor_app_read_file(
    BambuMonitorApp* app,
    const char* path,
    char** content_out,
    size_t* size_out);
static void bambu_monitor_app_log(BambuMonitorApp* app, const char* fmt, ...);
static void bambu_monitor_app_refresh_all_printers(BambuMonitorApp* app);
static void bambu_monitor_app_ensure_asset_dir(BambuMonitorApp* app);
static void bambu_monitor_app_load_path_settings(BambuMonitorApp* app);
static void bambu_monitor_app_save_path_settings(BambuMonitorApp* app);

static void bambu_monitor_app_main_view_draw(Canvas* canvas, void* model) {
    BambuMonitorMainViewModel* view_model = model;

    if(!view_model || !view_model->app) {
        return;
    }

    app_ui_draw(canvas, view_model->app);
}

static bool bambu_monitor_app_main_view_input(InputEvent* input_event, void* context) {
    BambuMonitorApp* app = context;

    if(!app || !input_event) {
        return false;
    }

    app_ui_handle_input(app, input_event);
    if(!app->running && app->view_dispatcher) {
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

static void bambu_monitor_app_copy_text(char* dst, size_t dst_size, const char* src) {
    if(!dst || dst_size == 0) {
        return;
    }

    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void bambu_monitor_app_log(BambuMonitorApp* app, const char* fmt, ...) {
    File* file = NULL;
    char line[256];
    va_list args;
    int line_len = 0;

    if(!app || !app->storage || !fmt) {
        return;
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        return;
    }

    if(!storage_file_open(file, BAMBU_MONITOR_SD_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_free(file);
        return;
    }

    line_len = snprintf(line, sizeof(line), "[%lu] ", (unsigned long)furi_get_tick());
    if(line_len < 0) {
        storage_file_close(file);
        storage_file_free(file);
        return;
    }

    va_start(args, fmt);
    vsnprintf(line + line_len, sizeof(line) - (size_t)line_len, fmt, args);
    va_end(args);

    storage_file_write(file, line, strlen(line));
    storage_file_write(file, "\n", 1U);
    storage_file_close(file);
    storage_file_free(file);
}

static void bambu_monitor_app_ensure_asset_dir(BambuMonitorApp* app) {
    if(!app || !app->storage) {
        return;
    }

    storage_simply_mkdir(app->storage, BAMBU_MONITOR_SD_ASSET_ROOT);
    storage_simply_mkdir(app->storage, BAMBU_MONITOR_SD_ASSET_DIR);
}

static void bambu_monitor_app_load_path_settings(BambuMonitorApp* app) {
    char* content = NULL;
    char* cursor = NULL;
    char* line_end = NULL;
    char saved_char = '\0';

    if(!app || !app->token_file_path || !app->printer_cache_path) {
        return;
    }

    if(!bambu_monitor_app_read_file(app, BAMBU_MONITOR_SD_SETTINGS_PATH, &content, NULL)) {
        return;
    }

    cursor = content;
    while(*cursor) {
        line_end = cursor;
        while(*line_end && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }

        saved_char = *line_end;
        *line_end = '\0';

        if(strncmp(cursor, "token_path=", 11) == 0) {
            const char* value = cursor + 11;
            if(*value) {
                furi_string_set(app->token_file_path, value);
            }
        } else if(strncmp(cursor, "cache_path=", 11) == 0) {
            const char* value = cursor + 11;
            if(*value) {
                furi_string_set(app->printer_cache_path, value);
            }
        }

        if(saved_char == '\0') {
            break;
        }

        cursor = line_end + 1;
        while(*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
    }

    free(content);
}

static void bambu_monitor_app_save_path_settings(BambuMonitorApp* app) {
    File* file = NULL;
    char content[512];
    int len = 0;

    if(!app || !app->storage || !app->token_file_path || !app->printer_cache_path) {
        return;
    }

    bambu_monitor_app_ensure_asset_dir(app);
    file = storage_file_alloc(app->storage);
    if(!file) {
        return;
    }

    if(!storage_file_open(file, BAMBU_MONITOR_SD_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(file);
        return;
    }

    len = snprintf(
        content,
        sizeof(content),
        "token_path=%s\ncache_path=%s\n",
        furi_string_get_cstr(app->token_file_path),
        furi_string_get_cstr(app->printer_cache_path));
    if(len > 0) {
        storage_file_write(file, content, (size_t)len);
    }
    storage_file_close(file);
    storage_file_free(file);
}

void bambu_monitor_app_request_redraw(BambuMonitorApp* app) {
    UNUSED(app);
}

void bambu_monitor_app_set_status(BambuMonitorApp* app, const char* status, const char* detail) {
    if(!app) {
        return;
    }

    bambu_monitor_app_copy_text(app->status_line, sizeof(app->status_line), status);
    bambu_monitor_app_copy_text(app->detail_line, sizeof(app->detail_line), detail);
}

static bool bambu_monitor_app_read_file(
    BambuMonitorApp* app,
    const char* path,
    char** content_out,
    size_t* size_out) {
    File* file = NULL;
    uint64_t file_size = 0;
    char* content = NULL;
    bool success = false;

    if(!app || !app->storage || !path || !content_out) {
        return false;
    }

    file = storage_file_alloc(app->storage);
    if(!file) {
        return false;
    }

    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    file_size = storage_file_size(file);
    if(file_size == 0 || file_size > 16384U) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    content = malloc((size_t)file_size + 1U);
    if(!content) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    if(storage_file_read(file, content, (uint16_t)file_size) != file_size) {
        free(content);
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    content[file_size] = '\0';
    *content_out = content;
    if(size_out) {
        *size_out = (size_t)file_size;
    }
    success = true;

    storage_file_close(file);
    storage_file_free(file);
    return success;
}

static bool bambu_monitor_app_extract_json_string(
    const char* object_text,
    const char* key,
    char* out,
    size_t out_size) {
    char pattern[48];
    const char* match = NULL;
    const char* start = NULL;
    const char* end = NULL;
    size_t len = 0;

    if(!object_text || !key || !out || out_size == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    match = strstr(object_text, pattern);
    if(!match) {
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        match = strstr(object_text, pattern);
        if(!match) {
            out[0] = '\0';
            return false;
        }
    }

    start = strchr(match, ':');
    if(!start) {
        out[0] = '\0';
        return false;
    }
    start++;
    while(*start == ' ' || *start == '\"') {
        if(*start == '\"') {
            start++;
            break;
        }
        start++;
    }

    end = strchr(start, '\"');
    if(!end) {
        out[0] = '\0';
        return false;
    }

    len = (size_t)(end - start);
    if(len >= out_size) {
        len = out_size - 1U;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool bambu_monitor_app_extract_json_bool(const char* object_text, const char* key, bool* value) {
    char pattern[48];
    const char* match = NULL;

    if(!object_text || !key || !value) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":true", key);
    match = strstr(object_text, pattern);
    if(match) {
        *value = true;
        return true;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\": true", key);
    match = strstr(object_text, pattern);
    if(match) {
        *value = true;
        return true;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":false", key);
    match = strstr(object_text, pattern);
    if(match) {
        *value = false;
        return true;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\": false", key);
    match = strstr(object_text, pattern);
    if(match) {
        *value = false;
        return true;
    }

    return false;
}

static bool bambu_monitor_app_load_printer_cache_from_path(BambuMonitorApp* app, const char* path) {
    char* content = NULL;
    const char* cursor = NULL;
    size_t count = 0;

    if(!bambu_monitor_app_read_file(app, path, &content, NULL)) {
        bambu_monitor_app_log(app, "cache_load read_failed path=%s", path ? path : "(null)");
        return false;
    }

    memset(app->transport.printers, 0, sizeof(app->transport.printers));
    app->transport.printer_count = 0;

    cursor = content;
    while((cursor = strstr(cursor, "\"serial\"")) != NULL &&
          count < BAMBU_MONITOR_MAX_PRINTERS) {
        const char* object_start = cursor;
        const char* object_end = strchr(cursor, '}');
        BambuPrinterInfo* printer = &app->transport.printers[count];

        while(object_start > content && *object_start != '{') {
            object_start--;
        }

        if(!object_end || *object_start != '{') {
            break;
        }

        memset(printer, 0, sizeof(BambuPrinterInfo));
        bambu_monitor_app_extract_json_string(object_start, "serial", printer->serial, sizeof(printer->serial));
        bambu_monitor_app_extract_json_string(object_start, "name", printer->name, sizeof(printer->name));
        bambu_monitor_app_extract_json_string(object_start, "model", printer->model, sizeof(printer->model));
        bambu_monitor_app_extract_json_string(object_start, "ip", printer->ip, sizeof(printer->ip));
        bambu_monitor_app_extract_json_string(
            object_start, "access_code", printer->access_code, sizeof(printer->access_code));
        bambu_monitor_app_extract_json_string(
            object_start, "print_status", printer->cloud_status, sizeof(printer->cloud_status));
        bambu_monitor_app_extract_json_bool(object_start, "online", &printer->online);

        if(printer->serial[0]) {
            count++;
        }

        cursor = object_end + 1;
    }

    app->transport.printer_count = count;
    bambu_monitor_app_log(
        app,
        "cache_load parsed path=%s printers=%u",
        path ? path : "(null)",
        (unsigned)count);
    free(content);
    return count > 0;
}

static bool bambu_monitor_app_load_printer_cache(BambuMonitorApp* app) {
    return bambu_monitor_app_load_printer_cache_from_path(app, BAMBU_MONITOR_SD_PRINTER_CACHE_PATH);
}

static void bambu_monitor_app_refresh_all_printers(BambuMonitorApp* app) {
    size_t index = 0;

    if(!app) {
        return;
    }

    for(index = 0; index < app->transport.printer_count; index++) {
        BambuPrinterInfo* printer = &app->transport.printers[index];
        bool success = false;

        if(!printer->serial[0]) {
            continue;
        }

        success = bambu_transport_refresh_status(&app->transport, printer->serial);
        bambu_monitor_app_log(
            app,
            "refresh_all success=%d serial=%s response=%s",
            success,
            printer->serial,
            app->transport.last_response);
    }
}

static bool bambu_monitor_app_load_token_from_path(BambuMonitorApp* app, const char* path) {
    char* content = NULL;
    char* cursor = NULL;
    char* token_start = NULL;

    if(!bambu_monitor_app_read_file(app, path, &content, NULL)) {
        bambu_monitor_app_log(app, "token_load read_failed path=%s", path ? path : "(null)");
        return false;
    }

    cursor = content;
    while(*cursor == ' ' || *cursor == '\r' || *cursor == '\n' || *cursor == '\t') {
        cursor++;
    }
    token_start = cursor;

    if(*cursor == '\0') {
        bambu_monitor_app_log(app, "token_load empty path=%s", path ? path : "(null)");
        free(content);
        return false;
    }

    while(*cursor && *cursor != '\r' && *cursor != '\n') {
        cursor++;
    }
    *cursor = '\0';

    bambu_monitor_app_copy_text(app->loaded_token, sizeof(app->loaded_token), token_start);
    bambu_monitor_app_log(
        app,
        "token_load ok path=%s len=%u",
        path ? path : "(null)",
        (unsigned)strlen(app->loaded_token));
    free(content);
    return app->loaded_token[0] != '\0';
}

static bool bambu_monitor_app_load_token_from_sd(BambuMonitorApp* app) {
    return bambu_monitor_app_load_token_from_path(app, BAMBU_MONITOR_SD_TOKEN_PATH);
}

void bambu_monitor_app_prompt_token_file(BambuMonitorApp* app) {
    DialogsFileBrowserOptions browser_options;

    if(!app || !app->dialogs || !app->token_file_path) {
        return;
    }

    dialog_file_browser_set_basic_options(&browser_options, ".txt", NULL);
    browser_options.base_path = BAMBU_MONITOR_SD_ASSET_DIR;

    if(dialog_file_browser_show(
           app->dialogs,
           app->token_file_path,
           app->token_file_path,
           &browser_options)) {
        bambu_monitor_app_save_path_settings(app);
        bambu_monitor_app_queue_busy_action(
            app,
            BambuMonitorCustomEventLoadTokenFromSd,
            "Load Token",
            "Reading selected token file");
    }
}

void bambu_monitor_app_prompt_printer_cache_file(BambuMonitorApp* app) {
    DialogsFileBrowserOptions browser_options;

    if(!app || !app->dialogs || !app->printer_cache_path) {
        return;
    }

    dialog_file_browser_set_basic_options(&browser_options, ".json", NULL);
    browser_options.base_path = BAMBU_MONITOR_SD_ASSET_DIR;

    if(dialog_file_browser_show(
           app->dialogs,
           app->printer_cache_path,
           app->printer_cache_path,
           &browser_options)) {
        bambu_monitor_app_save_path_settings(app);
        bambu_monitor_app_queue_busy_action(
            app,
            BambuMonitorCustomEventLoadPrintersFromSd,
            "Load Printers",
            "Reading selected printer cache");
    }
}

static void bambu_monitor_app_text_input_done(void* context) {
    BambuMonitorApp* app = context;

    if(!app) {
        return;
    }

    app->screen = BambuMonitorScreenWifiResults;
    view_dispatcher_switch_to_view(app->view_dispatcher, BambuMonitorViewMain);
    bambu_monitor_app_queue_busy_action(
        app,
        BambuMonitorCustomEventWifiConnectSelected,
        "WiFi Connect",
        app->pending_wifi_ssid);
}

static void bambu_monitor_app_show_keyboard_internal(BambuMonitorApp* app, const char* header) {
    if(!app || !app->text_input || !app->view_dispatcher) {
        return;
    }

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_minimum_length(app->text_input, 0U);
    text_input_set_result_callback(
        app->text_input,
        bambu_monitor_app_text_input_done,
        app,
        app->password_input,
        BAMBU_MONITOR_PASSWORD_SIZE,
        true);
    view_dispatcher_switch_to_view(app->view_dispatcher, BambuMonitorViewKeyboard);
}

void bambu_monitor_app_show_password_keyboard(BambuMonitorApp* app, const char* ssid) {
    if(!app) {
        return;
    }

    memset(app->password_input, 0, sizeof(app->password_input));
    bambu_monitor_app_copy_text(app->pending_wifi_ssid, sizeof(app->pending_wifi_ssid), ssid);
    bambu_monitor_app_show_keyboard_internal(app, "WiFi Password");
}

const char* bambu_monitor_app_connection_label(const BambuMonitorApp* app) {
    if(!app || !app->transport.initialized) {
        return "off";
    }

    if(app->transport.wifi_ssid[0]) {
        return app->transport.wifi_ssid;
    }

    return bambu_transport_is_ready(&app->transport) ? "bridge ok" : "bridge ?";
}

const BambuPrinterInfo* bambu_monitor_app_selected_printer(const BambuMonitorApp* app) {
    if(!app || app->printer_index >= app->transport.printer_count) {
        return NULL;
    }

    return &app->transport.printers[app->printer_index];
}

static void bambu_monitor_app_queue_busy_action(
    BambuMonitorApp* app,
    uint32_t event,
    const char* status,
    const char* detail) {
    if(!app || !app->view_dispatcher) {
        return;
    }

    bambu_monitor_app_set_status(app, status, detail);
    app->screen = BambuMonitorScreenBusy;
    view_dispatcher_switch_to_view(app->view_dispatcher, BambuMonitorViewMain);
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void bambu_monitor_app_queue_ping_bridge(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventPingBridge, "Ping Bridge", "Talking to ESP32");
}

void bambu_monitor_app_queue_scan_wifi(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventScanWifi, "WiFi Scan", "Bridge scanning nearby WiFi");
}

void bambu_monitor_app_queue_wifi_reconnect(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventWifiReconnect, "WiFi Reconnect", "Using saved WiFi credentials");
}

void bambu_monitor_app_queue_connect_selected_wifi(BambuMonitorApp* app) {
    const BambuWifiNetworkInfo* info = NULL;

    if(!app) {
        return;
    }

    if(app->transport.wifi_network_count == 0 || app->wifi_results_index >= app->transport.wifi_network_count) {
        bambu_monitor_app_set_status(app, "No WiFi", "Run WiFi scan first");
        app->screen = BambuMonitorScreenMainMenu;
        return;
    }

    info = &app->transport.wifi_networks[app->wifi_results_index];
    app->pending_wifi_index = app->wifi_results_index;
    bambu_monitor_app_copy_text(app->pending_wifi_ssid, sizeof(app->pending_wifi_ssid), info->ssid);

    if(info->secure) {
        bambu_monitor_app_show_password_keyboard(app, info->ssid);
        return;
    }

    memset(app->password_input, 0, sizeof(app->password_input));
    bambu_monitor_app_queue_busy_action(
        app,
        BambuMonitorCustomEventWifiConnectSelected,
        "WiFi Connect",
        info->ssid);
}

void bambu_monitor_app_queue_discover(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventDiscover, "Discover", "Fetching Bambu printers");
}

void bambu_monitor_app_queue_test_cloud_api(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventTestCloudApi, "Cloud Test", "Testing Bambu profile endpoint");
}

void bambu_monitor_app_queue_load_token_from_sd(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app, BambuMonitorCustomEventLoadTokenFromSd, "Load Token", "Reading bambu_token.txt");
}

void bambu_monitor_app_queue_load_printers_from_sd(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(
        app,
        BambuMonitorCustomEventLoadPrintersFromSd,
        "Load Printers",
        "Reading found_printers.json");
}

void bambu_monitor_app_queue_refresh_selected(BambuMonitorApp* app) {
    const BambuPrinterInfo* printer = bambu_monitor_app_selected_printer(app);
    const char* detail = printer ? printer->name : "No printer selected";
    bambu_monitor_app_queue_busy_action(
        app,
        BambuMonitorCustomEventRefreshSelected,
        "Refresh Status",
        detail);
}

static bool bambu_monitor_app_navigation_event(void* context) {
    BambuMonitorApp* app = context;
    if(app) {
        app->running = false;
    }
    return true;
}

static bool bambu_monitor_app_try_discover_with_token(BambuMonitorApp* app) {
    bool success = false;

    if(!app) {
        return false;
    }

    if(app->printer_cache_path && furi_string_size(app->printer_cache_path) > 0) {
        success = bambu_monitor_app_load_printer_cache_from_path(
            app,
            furi_string_get_cstr(app->printer_cache_path));
    } else {
        success = bambu_monitor_app_load_printer_cache(app);
    }

    if(!success) {
        bambu_monitor_app_log(app, "discover cache_missing");
        return false;
    }

    bambu_monitor_app_log(
        app,
        "discover begin local_mqtt printers=%u",
        (unsigned)app->transport.printer_count);
    bambu_monitor_app_refresh_all_printers(app);
    return true;
}

static bool bambu_monitor_app_handle_custom_event(void* context, uint32_t event) {
    BambuMonitorApp* app = context;
    bool success = false;
    const BambuPrinterInfo* printer = NULL;

    if(!app) {
        return false;
    }

    switch(event) {
    case BambuMonitorCustomEventPingBridge:
        success = bambu_transport_ping_bridge(&app->transport);
        bambu_monitor_app_log(app, "ping success=%d response=%s", success, app->transport.last_response);
        bambu_monitor_app_set_status(
            app,
            success ? "Bridge Online" : "Bridge Error",
            app->transport.last_response);
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventScanWifi:
        success = bambu_transport_scan_wifi(&app->transport);
        bambu_monitor_app_log(app, "wifi_scan success=%d response=%s", success, app->transport.last_response);
        bambu_monitor_app_set_status(
            app,
            success ? "WiFi Results" : "WiFi Scan Failed",
            app->transport.last_response);
        app->screen = success ? BambuMonitorScreenWifiResults : BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventWifiReconnect:
        success = bambu_transport_wifi_reconnect(&app->transport);
        if(success) {
            bambu_transport_wifi_status(&app->transport);
        }
        bambu_monitor_app_log(
            app,
            "wifi_reconnect success=%d response=%s ssid=%s ip=%s",
            success,
            app->transport.last_response,
            app->transport.wifi_ssid,
            app->transport.wifi_ip);
        bambu_monitor_app_set_status(
            app,
            success ? "WiFi Connected" : "WiFi Reconnect Failed",
            app->transport.last_response);
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventWifiConnectSelected:
        success = bambu_transport_wifi_connect(
            &app->transport,
            app->pending_wifi_ssid,
            app->password_input);
        if(success) {
            bambu_transport_wifi_status(&app->transport);
        }
        bambu_monitor_app_log(
            app,
            "wifi_connect success=%d ssid=%s response=%s",
            success,
            app->pending_wifi_ssid,
            app->transport.last_response);
        bambu_monitor_app_set_status(
            app,
            success ? "WiFi Connected" : "WiFi Connect Failed",
            app->transport.last_response);
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventLoadTokenFromSd:
        if(app->token_file_path && furi_string_size(app->token_file_path) > 0) {
            success = bambu_monitor_app_load_token_from_path(
                app,
                furi_string_get_cstr(app->token_file_path));
        } else {
            success = bambu_monitor_app_load_token_from_sd(app);
        }
        if(success) {
            success = bambu_transport_set_token(&app->transport, app->loaded_token);
        }
        bambu_monitor_app_log(
            app,
            "token_send success=%d len=%u response=%s",
            success,
            (unsigned)strlen(app->loaded_token),
            app->transport.last_response);
        bambu_monitor_app_set_status(
            app,
            success ? "Token Loaded" : "Token Load Failed",
            success ? "Token sent to bridge" : "Check selected token file");
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventTestCloudApi:
        success = bambu_transport_test_cloud_profile(&app->transport);
        bambu_monitor_app_log(
            app,
            "cloud_test success=%d response=%s",
            success,
            app->transport.last_response);
        bambu_monitor_app_set_status(
            app,
            success ? "Cloud API OK" : "Cloud API Failed",
            app->transport.last_response);
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventDiscover:
        success = bambu_monitor_app_try_discover_with_token(app);
        if(!success) {
            bambu_monitor_app_log(
                app,
                "discover failed response=%s printers=%u",
                app->transport.last_response,
                (unsigned)app->transport.printer_count);
            bambu_monitor_app_set_status(
                app,
                "Discover Failed",
                "Load or select found_printers.json");
        } else {
            bambu_monitor_app_log(
                app,
                "discover ok printers=%u response=%s",
                (unsigned)app->transport.printer_count,
                app->transport.last_response);
            bambu_monitor_app_set_status(
                app,
                "Printers Refreshed",
                "Loaded cache and updated via local MQTT");
        }
        app->screen = success ? BambuMonitorScreenPrinterList : BambuMonitorScreenMainMenu;
        if(app->printer_index >= app->transport.printer_count) {
            app->printer_index = 0;
        }
        break;
    case BambuMonitorCustomEventLoadPrintersFromSd:
        if(app->printer_cache_path && furi_string_size(app->printer_cache_path) > 0) {
            success = bambu_monitor_app_load_printer_cache_from_path(
                app,
                furi_string_get_cstr(app->printer_cache_path));
        } else {
            success = bambu_monitor_app_load_printer_cache(app);
        }
        bambu_monitor_app_log(
            app,
            "cache_load success=%d printers=%u",
            success,
            (unsigned)app->transport.printer_count);
        bambu_monitor_app_set_status(
            app,
            success ? "Cached Printers Loaded" : "Cache Load Failed",
            success ? "Using selected printer cache" : "Check selected JSON cache");
        if(success) {
            bambu_monitor_app_refresh_all_printers(app);
            bambu_monitor_app_set_status(app, "Printers Refreshed", "Loaded cache and updated MQTT status");
        }
        app->screen = success ? BambuMonitorScreenPrinterList : BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventRefreshSelected:
        printer = bambu_monitor_app_selected_printer(app);
        if(printer) {
            success = bambu_transport_refresh_status(&app->transport, printer->serial);
            bambu_monitor_app_log(
                app,
                "refresh success=%d serial=%s response=%s",
                success,
                printer->serial,
                app->transport.last_response);
            bambu_monitor_app_set_status(
                app,
                success ? "Status Updated" : "Refresh Failed",
                app->transport.last_response);
            app->screen = success ? BambuMonitorScreenPrinterDetail : BambuMonitorScreenPrinterList;
        } else {
            bambu_monitor_app_set_status(app, "No Printer", "Run discovery first");
            app->screen = BambuMonitorScreenMainMenu;
        }
        break;
    default:
        return false;
    }

    return true;
}

static void bambu_monitor_app_seed_defaults(BambuMonitorApp* app) {
    app->screen = BambuMonitorScreenMainMenu;
    app->running = true;
    app->main_menu_index = 0;
    app->wifi_results_index = 0;
    app->printer_index = 0;
    app->pending_wifi_index = 0;
    memset(app->password_input, 0, sizeof(app->password_input));
    memset(app->pending_wifi_ssid, 0, sizeof(app->pending_wifi_ssid));
    memset(app->loaded_token, 0, sizeof(app->loaded_token));
    bambu_monitor_app_set_status(app, "Ready", "WiFi, token, then discover");
}

int32_t bambu_monitor_app(void* p) {
    BambuMonitorApp* app = malloc(sizeof(BambuMonitorApp));
    BambuMonitorMainViewModel* model = NULL;
    int32_t result = 0;

    UNUSED(p);

    if(!app) {
        return -1;
    }

    memset(app, 0, sizeof(BambuMonitorApp));
    bambu_monitor_app_seed_defaults(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    bambu_monitor_app_ensure_asset_dir(app);

    if(!bambu_transport_init(&app->transport, bambu_transport_live_ops())) {
        bambu_monitor_app_log(app, "transport init_failed response=%s", app->transport.last_response);
        bambu_monitor_app_set_status(app, "Transport Init Failed", app->transport.last_response);
    } else {
        bambu_transport_wifi_status(&app->transport);
        bambu_monitor_app_log(
            app,
            "startup wifi_status response=%s ssid=%s ip=%s",
            app->transport.last_response,
            app->transport.wifi_ssid,
            app->transport.wifi_ip);
        if(bambu_monitor_app_load_token_from_sd(app)) {
            bambu_transport_set_token(&app->transport, app->loaded_token);
            bambu_monitor_app_log(
                app,
                "startup token_loaded len=%u response=%s",
                (unsigned)strlen(app->loaded_token),
                app->transport.last_response);
        }
    }

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();
    app->text_input = text_input_alloc();
    app->token_file_path = furi_string_alloc_set(BAMBU_MONITOR_SD_TOKEN_PATH);
    app->printer_cache_path = furi_string_alloc_set(BAMBU_MONITOR_SD_PRINTER_CACHE_PATH);
    bambu_monitor_app_load_path_settings(app);

    view_allocate_model(app->main_view, ViewModelTypeLockFree, sizeof(BambuMonitorMainViewModel));
    model = view_get_model(app->main_view);
    model->app = app;
    view_commit_model(app->main_view, false);

    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, bambu_monitor_app_main_view_draw);
    view_set_input_callback(app->main_view, bambu_monitor_app_main_view_input);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher,
        bambu_monitor_app_handle_custom_event);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher,
        bambu_monitor_app_navigation_event);
    view_dispatcher_add_view(app->view_dispatcher, BambuMonitorViewMain, app->main_view);
    view_dispatcher_add_view(
        app->view_dispatcher,
        BambuMonitorViewKeyboard,
        text_input_get_view(app->text_input));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, BambuMonitorViewMain);

    notification_internal_message(app->notifications, &sequence_set_only_blue_255);
    view_dispatcher_run(app->view_dispatcher);

    notification_internal_message(app->notifications, &sequence_reset_rgb);

    bambu_transport_deinit(&app->transport);

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, BambuMonitorViewMain);
        view_dispatcher_remove_view(app->view_dispatcher, BambuMonitorViewKeyboard);
        view_dispatcher_free(app->view_dispatcher);
    }
    if(app->text_input) {
        text_input_free(app->text_input);
    }
    if(app->token_file_path) {
        furi_string_free(app->token_file_path);
    }
    if(app->printer_cache_path) {
        furi_string_free(app->printer_cache_path);
    }
    if(app->main_view) {
        view_free(app->main_view);
    }

    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }
    if(app->dialogs) {
        furi_record_close(RECORD_DIALOGS);
    }
    if(app->storage) {
        furi_record_close(RECORD_STORAGE);
    }
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }

    free(app);
    return result;
}
