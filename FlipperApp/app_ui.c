#include "app_ui.h"

#include <stdio.h>
#include <string.h>

static const char* const bambu_main_menu_items[] = {
    "Ping Bridge",
    "WiFi Scan",
    "Discover Printers",
    "Printer List",
    "Refresh Selected",
    "About",
};

#define BAMBU_WIFI_VISIBLE_ITEMS 5U
#define BAMBU_PRINTER_VISIBLE_ITEMS 5U

static size_t bambu_ui_main_menu_count(void) {
    return sizeof(bambu_main_menu_items) / sizeof(bambu_main_menu_items[0]);
}

static void bambu_ui_draw_line(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str(canvas, 0, y, text ? text : "");
}

static void bambu_ui_draw_header(Canvas* canvas, const BambuMonitorApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 12, BAMBU_MONITOR_APP_NAME);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 82, 12, bambu_monitor_app_connection_label(app));
    bambu_ui_draw_line(canvas, 24, app->status_line);
}

static void bambu_ui_draw_main_menu(Canvas* canvas, BambuMonitorApp* app) {
    size_t first_visible = 0;

    if(app->main_menu_index >= BAMBU_MONITOR_MENU_VISIBLE_ITEMS) {
        first_visible = app->main_menu_index - (BAMBU_MONITOR_MENU_VISIBLE_ITEMS - 1U);
    }

    for(size_t i = 0; i < BAMBU_MONITOR_MENU_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        char line[BAMBU_MONITOR_STATUS_TEXT_SIZE];
        uint8_t y = 36 + (uint8_t)(i * 8U);

        if(item_index >= bambu_ui_main_menu_count()) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %s",
            (item_index == app->main_menu_index) ? '>' : ' ',
            bambu_main_menu_items[item_index]);
        bambu_ui_draw_line(canvas, y, line);
    }
}

static void bambu_ui_draw_busy(Canvas* canvas, BambuMonitorApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 16, app->status_line);
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 30, app->detail_line);
    bambu_ui_draw_line(canvas, 44, "Bridge action in progress");
    bambu_ui_draw_line(canvas, 54, "Please wait...");
}

static void bambu_ui_draw_wifi_results(Canvas* canvas, BambuMonitorApp* app) {
    size_t first_visible = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "WiFi Results");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 22, "Up/Down scroll  Back done");

    if(app->wifi_results_index >= BAMBU_WIFI_VISIBLE_ITEMS) {
        first_visible = app->wifi_results_index - (BAMBU_WIFI_VISIBLE_ITEMS - 1U);
    }

    if(app->transport.wifi_network_count == 0) {
        bambu_ui_draw_line(canvas, 40, "No networks returned");
        return;
    }

    for(size_t i = 0; i < BAMBU_WIFI_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        char line[BAMBU_MONITOR_WIFI_ENTRY_SIZE + 4U];
        uint8_t y = 34 + (uint8_t)(i * 8U);

        if(item_index >= app->transport.wifi_network_count) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %s",
            (item_index == app->wifi_results_index) ? '>' : ' ',
            app->transport.wifi_networks[item_index]);
        bambu_ui_draw_line(canvas, y, line);
    }
}

static void bambu_ui_draw_printer_list(Canvas* canvas, BambuMonitorApp* app) {
    size_t first_visible = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "Printers");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 22, "OK details  Back menu");

    if(app->printer_index >= BAMBU_PRINTER_VISIBLE_ITEMS) {
        first_visible = app->printer_index - (BAMBU_PRINTER_VISIBLE_ITEMS - 1U);
    }

    if(app->transport.printer_count == 0) {
        bambu_ui_draw_line(canvas, 40, "Run discovery first");
        return;
    }

    for(size_t i = 0; i < BAMBU_PRINTER_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        char line[BAMBU_MONITOR_NAME_SIZE + 8U];
        uint8_t y = 34 + (uint8_t)(i * 8U);

        if(item_index >= app->transport.printer_count) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %.22s",
            (item_index == app->printer_index) ? '>' : ' ',
            app->transport.printers[item_index].name);
        bambu_ui_draw_line(canvas, y, line);
    }
}

static void bambu_ui_draw_printer_detail(Canvas* canvas, BambuMonitorApp* app) {
    const BambuPrinterInfo* printer = bambu_monitor_app_selected_printer(app);
    char line[BAMBU_MONITOR_DETAIL_TEXT_SIZE];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "Printer Detail");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 22, "OK refresh  Back list");

    if(!printer) {
        bambu_ui_draw_line(canvas, 38, "No printer selected");
        return;
    }

    snprintf(line, sizeof(line), "Name: %.18s", printer->name);
    bambu_ui_draw_line(canvas, 32, line);
    snprintf(line, sizeof(line), "IP: %s", printer->ip[0] ? printer->ip : "?");
    bambu_ui_draw_line(canvas, 40, line);
    snprintf(line, sizeof(line), "State: %.16s", printer->state[0] ? printer->state : printer->cloud_status);
    bambu_ui_draw_line(canvas, 48, line);
    snprintf(line, sizeof(line), "Job: %u%% L%u/%u", printer->progress, printer->layer, printer->total_layers);
    bambu_ui_draw_line(canvas, 56, line);
    snprintf(line, sizeof(line), "N %.1f  B %.1f", (double)printer->nozzle_temp, (double)printer->bed_temp);
    bambu_ui_draw_line(canvas, 64, line);
}

static void bambu_ui_draw_about(Canvas* canvas) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "Bambu Fleet");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 26, "Flipper UI + ESP32 bridge");
    bambu_ui_draw_line(canvas, 36, "UART on pins 13/14");
    bambu_ui_draw_line(canvas, 46, "Bridge handles WiFi/API");
    bambu_ui_draw_line(canvas, 56, "Scaffold for Bambu fleet");
}

void app_ui_draw(Canvas* canvas, BambuMonitorApp* app) {
    if(!canvas || !app) {
        return;
    }

    if(app->screen == BambuMonitorScreenBusy) {
        bambu_ui_draw_busy(canvas, app);
        return;
    }

    switch(app->screen) {
    case BambuMonitorScreenMainMenu:
        bambu_ui_draw_header(canvas, app);
        bambu_ui_draw_main_menu(canvas, app);
        break;
    case BambuMonitorScreenWifiResults:
        bambu_ui_draw_wifi_results(canvas, app);
        break;
    case BambuMonitorScreenPrinterList:
        bambu_ui_draw_printer_list(canvas, app);
        break;
    case BambuMonitorScreenPrinterDetail:
        bambu_ui_draw_printer_detail(canvas, app);
        break;
    case BambuMonitorScreenAbout:
        bambu_ui_draw_about(canvas);
        break;
    default:
        break;
    }
}

static bool bambu_ui_accept_input(const InputEvent* input_event) {
    return input_event->type == InputTypeShort || input_event->type == InputTypeRepeat;
}

static void bambu_ui_handle_main_menu(BambuMonitorApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->main_menu_index > 0) {
            app->main_menu_index--;
        }
        break;
    case InputKeyDown:
        if(app->main_menu_index + 1U < bambu_ui_main_menu_count()) {
            app->main_menu_index++;
        }
        break;
    case InputKeyOk:
        switch(app->main_menu_index) {
        case 0:
            bambu_monitor_app_queue_ping_bridge(app);
            break;
        case 1:
            bambu_monitor_app_queue_scan_wifi(app);
            break;
        case 2:
            bambu_monitor_app_queue_discover(app);
            break;
        case 3:
            app->screen = BambuMonitorScreenPrinterList;
            break;
        case 4:
            bambu_monitor_app_queue_refresh_selected(app);
            break;
        case 5:
            app->screen = BambuMonitorScreenAbout;
            break;
        default:
            break;
        }
        break;
    case InputKeyBack:
        app->running = false;
        break;
    default:
        break;
    }
}

static void bambu_ui_handle_wifi_results(BambuMonitorApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->wifi_results_index > 0) {
            app->wifi_results_index--;
        }
        break;
    case InputKeyDown:
        if(app->wifi_results_index + 1U < app->transport.wifi_network_count) {
            app->wifi_results_index++;
        }
        break;
    case InputKeyOk:
    case InputKeyBack:
        app->screen = BambuMonitorScreenMainMenu;
        break;
    default:
        break;
    }
}

static void bambu_ui_handle_printer_list(BambuMonitorApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyUp:
        if(app->printer_index > 0) {
            app->printer_index--;
        }
        break;
    case InputKeyDown:
        if(app->printer_index + 1U < app->transport.printer_count) {
            app->printer_index++;
        }
        break;
    case InputKeyOk:
        app->screen = BambuMonitorScreenPrinterDetail;
        break;
    case InputKeyBack:
        app->screen = BambuMonitorScreenMainMenu;
        break;
    default:
        break;
    }
}

static void bambu_ui_handle_printer_detail(BambuMonitorApp* app, const InputEvent* input_event) {
    switch(input_event->key) {
    case InputKeyOk:
        bambu_monitor_app_queue_refresh_selected(app);
        break;
    case InputKeyLeft:
        if(app->printer_index > 0) {
            app->printer_index--;
        }
        break;
    case InputKeyRight:
        if(app->printer_index + 1U < app->transport.printer_count) {
            app->printer_index++;
        }
        break;
    case InputKeyBack:
        app->screen = BambuMonitorScreenPrinterList;
        break;
    default:
        break;
    }
}

static void bambu_ui_handle_about(BambuMonitorApp* app, const InputEvent* input_event) {
    if(input_event->key == InputKeyOk || input_event->key == InputKeyBack) {
        app->screen = BambuMonitorScreenMainMenu;
    }
}

void app_ui_handle_input(BambuMonitorApp* app, const InputEvent* input_event) {
    if(!app || !input_event || !bambu_ui_accept_input(input_event)) {
        return;
    }

    switch(app->screen) {
    case BambuMonitorScreenMainMenu:
        bambu_ui_handle_main_menu(app, input_event);
        break;
    case BambuMonitorScreenWifiResults:
        bambu_ui_handle_wifi_results(app, input_event);
        break;
    case BambuMonitorScreenPrinterList:
        bambu_ui_handle_printer_list(app, input_event);
        break;
    case BambuMonitorScreenPrinterDetail:
        bambu_ui_handle_printer_detail(app, input_event);
        break;
    case BambuMonitorScreenAbout:
        bambu_ui_handle_about(app, input_event);
        break;
    case BambuMonitorScreenBusy:
        break;
    default:
        break;
    }

    bambu_monitor_app_request_redraw(app);
}
