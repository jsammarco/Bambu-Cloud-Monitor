#include "app_ui.h"

#include <stdio.h>
#include <string.h>

static const char* const bambu_main_menu_items[] = {
    "Ping Bridge",
    "WiFi Scan",
    "Connect Last WiFi",
    "Connect Selected WiFi",
    "Load Token from SD",
    "Test Cloud API",
    "Discover Printers",
    "Load Printers from SD",
    "Printer List",
    "Refresh Selected",
    "About",
};

#define BAMBU_WIFI_VISIBLE_ITEMS 4U
#define BAMBU_PRINTER_VISIBLE_ITEMS 4U
#define BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS 4U
#define BAMBU_PRINTER_DETAIL_LINE_COUNT 11U

static size_t bambu_ui_main_menu_count(void) {
    return sizeof(bambu_main_menu_items) / sizeof(bambu_main_menu_items[0]);
}

static void bambu_ui_draw_line(Canvas* canvas, uint8_t y, const char* text) {
    canvas_draw_str(canvas, 0, y, text ? text : "");
}

static const char* bambu_ui_unknown_text(bool has_status, const char* value) {
    return (has_status && value && value[0]) ? value : "??";
}

static void bambu_ui_format_optional_u16(char* out, size_t out_size, bool is_known, uint16_t value) {
    if(!is_known) {
        snprintf(out, out_size, "??");
        return;
    }

    snprintf(out, out_size, "%u", value);
}

static void bambu_ui_format_optional_temp(char* out, size_t out_size, bool is_known, float value) {
    if(!is_known) {
        snprintf(out, out_size, "??");
        return;
    }

    snprintf(out, out_size, "%.1f", (double)value);
}

static void bambu_ui_draw_header(Canvas* canvas, const BambuMonitorApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 12, BAMBU_MONITOR_APP_NAME);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 72, 12, bambu_monitor_app_connection_label(app));
    bambu_ui_draw_line(canvas, 22, app->status_line);
    bambu_ui_draw_line(canvas, 30, app->detail_line);
}

static void bambu_ui_draw_main_menu(Canvas* canvas, BambuMonitorApp* app) {
    size_t first_visible = 0;

    if(app->main_menu_index >= BAMBU_MONITOR_MENU_VISIBLE_ITEMS) {
        first_visible = app->main_menu_index - (BAMBU_MONITOR_MENU_VISIBLE_ITEMS - 1U);
    }

    for(size_t i = 0; i < BAMBU_MONITOR_MENU_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        char line[BAMBU_MONITOR_STATUS_TEXT_SIZE];
        uint8_t y = 38 + (uint8_t)(i * 8U);

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
    bambu_ui_draw_line(canvas, 14, app->status_line);
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 26, app->detail_line);
    bambu_ui_draw_line(canvas, 42, "Bridge action in progress");
    bambu_ui_draw_line(canvas, 52, "Please wait...");
}

static void bambu_ui_draw_wifi_results(Canvas* canvas, BambuMonitorApp* app) {
    size_t first_visible = 0;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "WiFi Results");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 22, "OK connect  Back done");

    if(app->wifi_results_index >= BAMBU_WIFI_VISIBLE_ITEMS) {
        first_visible = app->wifi_results_index - (BAMBU_WIFI_VISIBLE_ITEMS - 1U);
    }

    if(app->transport.wifi_network_count == 0) {
        bambu_ui_draw_line(canvas, 40, "No networks returned");
        return;
    }

    for(size_t i = 0; i < BAMBU_WIFI_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        char line[BAMBU_MONITOR_WIFI_ENTRY_SIZE + 8U];
        uint8_t y = 32 + (uint8_t)(i * 8U);
        const BambuWifiNetworkInfo* info = NULL;

        if(item_index >= app->transport.wifi_network_count) {
            break;
        }

        info = &app->transport.wifi_networks[item_index];
        snprintf(
            line,
            sizeof(line),
            "%c %.20s %c",
            (item_index == app->wifi_results_index) ? '>' : ' ',
            info->ssid,
            info->secure ? '*' : ' ');
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
        uint8_t y = 32 + (uint8_t)(i * 8U);

        if(item_index >= app->transport.printer_count) {
            break;
        }

        snprintf(
            line,
            sizeof(line),
            "%c %.20s",
            (item_index == app->printer_index) ? '>' : ' ',
            app->transport.printers[item_index].name);
        bambu_ui_draw_line(canvas, y, line);
    }
}

static void bambu_ui_draw_printer_detail(Canvas* canvas, BambuMonitorApp* app) {
    const BambuPrinterInfo* printer = bambu_monitor_app_selected_printer(app);
    const char* lines[BAMBU_PRINTER_DETAIL_LINE_COUNT];
    char line_buf[BAMBU_PRINTER_DETAIL_LINE_COUNT][BAMBU_MONITOR_DETAIL_TEXT_SIZE];
    size_t line_count = 0;
    size_t first_visible = 0;
    char value_a[16];
    char value_b[16];
    char value_c[16];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "Printer Detail");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 22, "OK refresh Up/Dn scroll");

    if(!printer) {
        bambu_ui_draw_line(canvas, 38, "No printer selected");
        return;
    }

    snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Name: %.18s", printer->name);
    lines[line_count] = line_buf[line_count];
    line_count++;

    snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "IP: %s", printer->ip[0] ? printer->ip : "??");
    lines[line_count] = line_buf[line_count];
    line_count++;

    snprintf(
        line_buf[line_count],
        sizeof(line_buf[line_count]),
        "State: %.16s",
        bambu_ui_unknown_text(printer->has_status, printer->state));
    lines[line_count] = line_buf[line_count];
    line_count++;

    if(printer->has_progress || printer->has_layer || printer->has_total_layers) {
        bambu_ui_format_optional_u16(value_a, sizeof(value_a), printer->has_progress, printer->progress);
        bambu_ui_format_optional_u16(value_b, sizeof(value_b), printer->has_layer, printer->layer);
        bambu_ui_format_optional_u16(value_c, sizeof(value_c), printer->has_total_layers, printer->total_layers);
        snprintf(
            line_buf[line_count],
            sizeof(line_buf[line_count]),
            "Job: %s%% L%s/%s",
            value_a,
            value_b,
            value_c);
    } else {
        snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Job: %s L%s/%s", "??", "??", "??");
    }
    lines[line_count] = line_buf[line_count];
    line_count++;

    if(printer->has_remaining_minutes) {
        snprintf(
            line_buf[line_count],
            sizeof(line_buf[line_count]),
            "Remain: %u min",
            printer->remaining_minutes);
    } else {
        snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Remain: ?? min");
    }
    lines[line_count] = line_buf[line_count];
    line_count++;

    bambu_ui_format_optional_temp(value_a, sizeof(value_a), printer->has_nozzle_temp, printer->nozzle_temp);
    snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Nozzle: %s C", value_a);
    lines[line_count] = line_buf[line_count];
    line_count++;
    bambu_ui_format_optional_temp(value_a, sizeof(value_a), printer->has_bed_temp, printer->bed_temp);
    snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Bed: %s C", value_a);
    lines[line_count] = line_buf[line_count];
    line_count++;
    snprintf(
        line_buf[line_count],
        sizeof(line_buf[line_count]),
        "WiFi: %.14s",
        bambu_ui_unknown_text(printer->has_status, printer->wifi_signal));
    lines[line_count] = line_buf[line_count];
    line_count++;

    bambu_ui_format_optional_u16(value_a, sizeof(value_a), printer->has_speed, printer->speed);
    snprintf(line_buf[line_count], sizeof(line_buf[line_count]), "Speed: %s", value_a);
    lines[line_count] = line_buf[line_count];
    line_count++;

    bambu_ui_format_optional_u16(value_a, sizeof(value_a), printer->has_fan, printer->fan);
    bambu_ui_format_optional_u16(value_b, sizeof(value_b), printer->has_fan_aux1, printer->fan_aux1);
    bambu_ui_format_optional_u16(value_c, sizeof(value_c), printer->has_fan_aux2, printer->fan_aux2);
    snprintf(
        line_buf[line_count],
        sizeof(line_buf[line_count]),
        "Fans: %s/%s/%s",
        value_a,
        value_b,
        value_c);
    lines[line_count] = line_buf[line_count];
    line_count++;

    snprintf(
        line_buf[line_count],
        sizeof(line_buf[line_count]),
        "File: %.14s",
        bambu_ui_unknown_text(printer->has_status, printer->file_name));
    lines[line_count] = line_buf[line_count];
    line_count++;

    if(line_count > BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS) {
        size_t max_scroll = line_count - BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS;
        first_visible = (app->detail_scroll > max_scroll) ? max_scroll : app->detail_scroll;
    } else {
        first_visible = 0;
    }

    for(size_t i = 0; i < BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS; i++) {
        size_t item_index = first_visible + i;
        uint8_t y = 32 + (uint8_t)(i * 8U);
        if(item_index >= line_count) {
            break;
        }
        bambu_ui_draw_line(canvas, y, lines[item_index]);
    }

    if(first_visible > 0) {
        canvas_draw_str(canvas, 120, 30, "^");
    }

    if(first_visible + BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS < line_count) {
        canvas_draw_str(canvas, 120, 62, "v");
    }
}

static void bambu_ui_draw_about(Canvas* canvas) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    bambu_ui_draw_line(canvas, 12, "Bambu Fleet");
    canvas_set_font(canvas, FontSecondary);
    bambu_ui_draw_line(canvas, 24, "Multi-printer dashboard");
    bambu_ui_draw_line(canvas, 34, "ESP32 bridge for WiFi,");
    bambu_ui_draw_line(canvas, 42, "cloud API, and status");
    bambu_ui_draw_line(canvas, 54, "ConsultingJoe.com");
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
            bambu_monitor_app_queue_wifi_reconnect(app);
            break;
        case 3:
            bambu_monitor_app_queue_connect_selected_wifi(app);
            break;
        case 4:
            bambu_monitor_app_prompt_token_file(app);
            break;
        case 5:
            bambu_monitor_app_queue_test_cloud_api(app);
            break;
        case 6:
            bambu_monitor_app_queue_discover(app);
            break;
        case 7:
            bambu_monitor_app_prompt_printer_cache_file(app);
            break;
        case 8:
            app->screen = BambuMonitorScreenPrinterList;
            break;
        case 9:
            bambu_monitor_app_queue_refresh_selected(app);
            break;
        case 10:
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
        bambu_monitor_app_queue_connect_selected_wifi(app);
        break;
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
        app->detail_scroll = 0;
        break;
    case InputKeyBack:
        app->screen = BambuMonitorScreenMainMenu;
        break;
    default:
        break;
    }
}

static void bambu_ui_handle_printer_detail(BambuMonitorApp* app, const InputEvent* input_event) {
    size_t max_scroll = 0;

    if(app->transport.printer_count > 0 && BAMBU_PRINTER_DETAIL_LINE_COUNT > BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS) {
        max_scroll = BAMBU_PRINTER_DETAIL_LINE_COUNT - BAMBU_PRINTER_DETAIL_VISIBLE_ITEMS;
    }

    switch(input_event->key) {
    case InputKeyUp:
        if(app->detail_scroll > 0) {
            app->detail_scroll--;
        }
        break;
    case InputKeyDown:
        if(app->detail_scroll < max_scroll) {
            app->detail_scroll++;
        }
        break;
    case InputKeyOk:
        bambu_monitor_app_queue_refresh_selected(app);
        break;
    case InputKeyLeft:
        if(app->printer_index > 0) {
            app->printer_index--;
            app->detail_scroll = 0;
        }
        break;
    case InputKeyRight:
        if(app->printer_index + 1U < app->transport.printer_count) {
            app->printer_index++;
            app->detail_scroll = 0;
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

    app->last_input_tick = furi_get_tick();

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
