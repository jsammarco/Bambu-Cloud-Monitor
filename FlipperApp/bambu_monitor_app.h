#pragma once

#include "bambu_monitor_config.h"
#include "bambu_transport.h"
#include "wifi_password_input.h"

#include <furi.h>
#include <dialogs/dialogs.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <notification/notification.h>
#include <storage/storage.h>

typedef enum {
    BambuMonitorScreenMainMenu = 0,
    BambuMonitorScreenBusy,
    BambuMonitorScreenWifiResults,
    BambuMonitorScreenPrinterList,
    BambuMonitorScreenPrinterDetail,
    BambuMonitorScreenAbout,
} BambuMonitorScreen;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    NotificationApp* notifications;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    WifiPasswordInput* text_input;
    FuriString* token_file_path;
    FuriString* printer_cache_path;
    BambuTransport transport;
    BambuMonitorScreen screen;
    bool running;
    size_t main_menu_index;
    size_t wifi_results_index;
    size_t printer_index;
    size_t detail_scroll;
    size_t pending_wifi_index;
    uint32_t last_input_tick;
    uint32_t last_detail_refresh_tick;
    char password_input[BAMBU_MONITOR_PASSWORD_SIZE];
    char pending_wifi_ssid[BAMBU_MONITOR_WIFI_SSID_SIZE];
    char loaded_token[BAMBU_MONITOR_TOKEN_SIZE];
    char status_line[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    char detail_line[BAMBU_MONITOR_DETAIL_TEXT_SIZE];
} BambuMonitorApp;

int32_t bambu_monitor_app(void* p);

void bambu_monitor_app_request_redraw(BambuMonitorApp* app);
void bambu_monitor_app_set_status(BambuMonitorApp* app, const char* status, const char* detail);
void bambu_monitor_app_queue_ping_bridge(BambuMonitorApp* app);
void bambu_monitor_app_queue_scan_wifi(BambuMonitorApp* app);
void bambu_monitor_app_queue_wifi_reconnect(BambuMonitorApp* app);
void bambu_monitor_app_queue_connect_selected_wifi(BambuMonitorApp* app);
void bambu_monitor_app_queue_discover(BambuMonitorApp* app);
void bambu_monitor_app_queue_load_token_from_sd(BambuMonitorApp* app);
void bambu_monitor_app_queue_test_cloud_api(BambuMonitorApp* app);
void bambu_monitor_app_queue_load_printers_from_sd(BambuMonitorApp* app);
void bambu_monitor_app_queue_refresh_selected(BambuMonitorApp* app);
void bambu_monitor_app_show_password_keyboard(BambuMonitorApp* app, const char* ssid);
void bambu_monitor_app_prompt_token_file(BambuMonitorApp* app);
void bambu_monitor_app_prompt_printer_cache_file(BambuMonitorApp* app);
const char* bambu_monitor_app_connection_label(const BambuMonitorApp* app);
const BambuPrinterInfo* bambu_monitor_app_selected_printer(const BambuMonitorApp* app);
