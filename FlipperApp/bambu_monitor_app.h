#pragma once

#include "bambu_monitor_config.h"
#include "bambu_transport.h"

#include <furi.h>
#include <dialogs/dialogs.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <notification/notification.h>

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
    ViewDispatcher* view_dispatcher;
    View* main_view;
    BambuTransport transport;
    BambuMonitorScreen screen;
    bool running;
    size_t main_menu_index;
    size_t wifi_results_index;
    size_t printer_index;
    char status_line[BAMBU_MONITOR_STATUS_TEXT_SIZE];
    char detail_line[BAMBU_MONITOR_DETAIL_TEXT_SIZE];
} BambuMonitorApp;

int32_t bambu_monitor_app(void* p);

void bambu_monitor_app_request_redraw(BambuMonitorApp* app);
void bambu_monitor_app_set_status(BambuMonitorApp* app, const char* status, const char* detail);
void bambu_monitor_app_queue_ping_bridge(BambuMonitorApp* app);
void bambu_monitor_app_queue_scan_wifi(BambuMonitorApp* app);
void bambu_monitor_app_queue_discover(BambuMonitorApp* app);
void bambu_monitor_app_queue_refresh_selected(BambuMonitorApp* app);
const char* bambu_monitor_app_connection_label(const BambuMonitorApp* app);
const BambuPrinterInfo* bambu_monitor_app_selected_printer(const BambuMonitorApp* app);
