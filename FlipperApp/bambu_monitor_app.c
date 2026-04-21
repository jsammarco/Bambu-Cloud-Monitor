#include "bambu_monitor_app.h"

#include "app_ui.h"

#include <notification/notification_messages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    BambuMonitorViewMain = 0,
} BambuMonitorViewId;

typedef enum {
    BambuMonitorCustomEventPingBridge = 1,
    BambuMonitorCustomEventScanWifi,
    BambuMonitorCustomEventDiscover,
    BambuMonitorCustomEventRefreshSelected,
} BambuMonitorCustomEvent;

typedef struct {
    BambuMonitorApp* app;
} BambuMonitorMainViewModel;

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

void bambu_monitor_app_request_redraw(BambuMonitorApp* app) {
    UNUSED(app);
}

void bambu_monitor_app_set_status(BambuMonitorApp* app, const char* status, const char* detail) {
    if(!app) {
        return;
    }

    snprintf(app->status_line, sizeof(app->status_line), "%s", status ? status : "");
    snprintf(app->detail_line, sizeof(app->detail_line), "%s", detail ? detail : "");
}

const char* bambu_monitor_app_connection_label(const BambuMonitorApp* app) {
    if(!app) {
        return "n/a";
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
    bambu_monitor_app_request_redraw(app);
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void bambu_monitor_app_queue_ping_bridge(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(app, BambuMonitorCustomEventPingBridge, "Ping Bridge", "Talking to ESP32");
}

void bambu_monitor_app_queue_scan_wifi(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(app, BambuMonitorCustomEventScanWifi, "WiFi Scan", "Bridge scanning nearby WiFi");
}

void bambu_monitor_app_queue_discover(BambuMonitorApp* app) {
    bambu_monitor_app_queue_busy_action(app, BambuMonitorCustomEventDiscover, "Discover", "Fetching Bambu printers");
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
        bambu_monitor_app_set_status(
            app,
            success ? "Bridge Online" : "Bridge Error",
            app->transport.last_response);
        app->screen = BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventScanWifi:
        success = bambu_transport_scan_wifi(&app->transport);
        bambu_monitor_app_set_status(
            app,
            success ? "WiFi Results" : "WiFi Scan Failed",
            app->transport.last_response);
        app->screen = success ? BambuMonitorScreenWifiResults : BambuMonitorScreenMainMenu;
        break;
    case BambuMonitorCustomEventDiscover:
        success = bambu_transport_discover_printers(&app->transport);
        bambu_monitor_app_set_status(
            app,
            success ? "Printers Loaded" : "Discover Failed",
            app->transport.last_response);
        app->screen = success ? BambuMonitorScreenPrinterList : BambuMonitorScreenMainMenu;
        if(app->printer_index >= app->transport.printer_count) {
            app->printer_index = 0;
        }
        break;
    case BambuMonitorCustomEventRefreshSelected:
        printer = bambu_monitor_app_selected_printer(app);
        if(printer) {
            success = bambu_transport_refresh_status(&app->transport, printer->serial);
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

    bambu_monitor_app_request_redraw(app);
    return true;
}

static void bambu_monitor_app_seed_defaults(BambuMonitorApp* app) {
    app->screen = BambuMonitorScreenMainMenu;
    app->running = true;
    app->main_menu_index = 0;
    app->wifi_results_index = 0;
    app->printer_index = 0;
    bambu_monitor_app_set_status(app, "Ready", "Ping bridge or discover printers");
}

int32_t bambu_monitor_app(void* p) {
    UNUSED(p);

    BambuMonitorApp* app = malloc(sizeof(BambuMonitorApp));
    BambuMonitorMainViewModel* model = NULL;
    int32_t result = 0;

    if(!app) {
        return -1;
    }

    memset(app, 0, sizeof(BambuMonitorApp));
    bambu_monitor_app_seed_defaults(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    if(!bambu_transport_init(&app->transport, bambu_transport_live_ops())) {
        bambu_monitor_app_set_status(app, "Transport Init Failed", app->transport.last_response);
    }

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_view = view_alloc();

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
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, BambuMonitorViewMain);

    notification_internal_message(app->notifications, &sequence_set_only_blue_255);
    view_dispatcher_run(app->view_dispatcher);

    notification_internal_message(app->notifications, &sequence_reset_rgb);

    bambu_transport_deinit(&app->transport);

    if(app->view_dispatcher) {
        view_dispatcher_remove_view(app->view_dispatcher, BambuMonitorViewMain);
        view_dispatcher_free(app->view_dispatcher);
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
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }

    free(app);
    return result;
}
