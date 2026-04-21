#pragma once

#include "bambu_monitor_app.h"

void app_ui_draw(Canvas* canvas, BambuMonitorApp* app);
void app_ui_handle_input(BambuMonitorApp* app, const InputEvent* input_event);
