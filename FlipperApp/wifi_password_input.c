#include "wifi_password_input.h"

#include <assets_icons.h>
#include <furi.h>
#include <gui/elements.h>
#include <string.h>

struct WifiPasswordInput {
    View* view;
    FuriTimer* timer;
};

typedef struct {
    const char text;
    const uint8_t x;
    const uint8_t y;
} WifiPasswordInputKey;

typedef struct {
    const char* header;
    char* text_buffer;
    size_t text_buffer_size;
    size_t minimum_length;
    bool clear_default_text;

    WifiPasswordInputCallback callback;
    void* callback_context;

    uint8_t selected_row;
    uint8_t selected_column;

    WifiPasswordInputValidatorCallback validator_callback;
    void* validator_callback_context;
    FuriString* validator_text;
    bool validator_message_visible;
} WifiPasswordInputModel;

static const uint8_t keyboard_origin_x = 0;
static const uint8_t keyboard_origin_y = 29;
static const uint8_t keyboard_row_count = 3;

#define ENTER_KEY     '\r'
#define BACKSPACE_KEY '\b'

static const WifiPasswordInputKey keyboard_keys_row_1[] = {
    {'q', 1, 8},   {'w', 9, 8},   {'e', 17, 8},  {'r', 25, 8},
    {'t', 33, 8},  {'y', 41, 8},  {'u', 49, 8},  {'i', 57, 8},
    {'o', 65, 8},  {'p', 73, 8},  {'1', 81, 8},  {'2', 89, 8},
    {'3', 97, 8},  {'4', 105, 8}, {'5', 113, 8}, {'6', 121, 8},
};

static const WifiPasswordInputKey keyboard_keys_row_2[] = {
    {'a', 1, 20},    {'s', 9, 20},   {'d', 17, 20},  {'f', 25, 20},
    {'g', 33, 20},   {'h', 41, 20},  {'j', 49, 20},  {'k', 57, 20},
    {'l', 65, 20},   {'0', 73, 20},  {'7', 81, 20},  {'8', 89, 20},
    {'9', 97, 20},   {'*', 105, 20}, {BACKSPACE_KEY, 112, 12},
};

static const WifiPasswordInputKey keyboard_keys_row_3[] = {
    {'z', 1, 32},   {'x', 9, 32},   {'c', 17, 32},  {'v', 25, 32},
    {'b', 33, 32},  {'n', 41, 32},  {'m', 49, 32},  {'_', 57, 32},
    {'-', 65, 32},  {'.', 73, 32},  {'@', 81, 32},  {'!', 89, 32},
    {'?', 97, 32},  {ENTER_KEY, 104, 23},
};

static uint8_t get_row_size(uint8_t row_index) {
    switch(row_index + 1) {
    case 1:
        return COUNT_OF(keyboard_keys_row_1);
    case 2:
        return COUNT_OF(keyboard_keys_row_2);
    case 3:
        return COUNT_OF(keyboard_keys_row_3);
    default:
        furi_crash();
    }
}

static const WifiPasswordInputKey* get_row(uint8_t row_index) {
    switch(row_index + 1) {
    case 1:
        return keyboard_keys_row_1;
    case 2:
        return keyboard_keys_row_2;
    case 3:
        return keyboard_keys_row_3;
    default:
        furi_crash();
    }
}

static char get_selected_char(WifiPasswordInputModel* model) {
    return get_row(model->selected_row)[model->selected_column].text;
}

static bool char_is_lowercase(char letter) {
    return letter >= 'a' && letter <= 'z';
}

static char get_output_char(char selected, bool shift) {
    if(!shift) {
        return selected;
    }

    if(selected == '_') {
        return ' ';
    }

    if(char_is_lowercase(selected)) {
        return selected - 0x20;
    }

    switch(selected) {
    case '1':
        return '~';
    case '2':
        return '#';
    case '3':
        return '$';
    case '4':
        return '%';
    case '5':
        return '^';
    case '6':
        return '&';
    case '7':
        return '(';
    case '8':
        return '*';
    case '9':
        return ')';
    case '0':
        return '=';
    case '*':
        return '+';
    case '-':
        return '/';
    case '.':
        return ':';
    case '@':
        return '\\';
    case '!':
        return '\'';
    case '?':
        return '"';
    default:
        return selected;
    }
}

static void wifi_password_input_backspace_cb(WifiPasswordInputModel* model) {
    if(!model->text_buffer || model->text_buffer_size == 0) {
        return;
    }

    uint8_t text_length =
        (!model->text_buffer || model->clear_default_text) ? 1 : strlen(model->text_buffer);
    if(text_length > 0) {
        model->text_buffer[text_length - 1] = 0;
    }
}

static void wifi_password_input_draw_key(
    Canvas* canvas,
    const WifiPasswordInputModel* model,
    const WifiPasswordInputKey* key,
    uint8_t row,
    uint8_t column) {
    if(key->text == ENTER_KEY) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(
            canvas,
            keyboard_origin_x + key->x,
            keyboard_origin_y + key->y,
            (model->selected_row == row && model->selected_column == column) ? &I_KeySaveSelected_22x11 :
                                                                                &I_KeySave_22x11);
        return;
    }

    if(key->text == BACKSPACE_KEY) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(
            canvas,
            keyboard_origin_x + key->x,
            keyboard_origin_y + key->y,
            (model->selected_row == row && model->selected_column == column) ?
                &I_KeyBackspaceSelected_17x11 :
                &I_KeyBackspace_17x11);
        return;
    }

    if(model->selected_row == row && model->selected_column == column) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, keyboard_origin_x + key->x - 1, keyboard_origin_y + key->y - 8, 7, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_glyph(canvas, keyboard_origin_x + key->x, keyboard_origin_y + key->y, key->text);
}

static void wifi_password_input_view_draw_callback(Canvas* canvas, void* _model) {
    WifiPasswordInputModel* model = _model;
    uint8_t needed_string_width = canvas_width(canvas) - 8;
    uint8_t start_pos = 4;
    const char* text = model->text_buffer ? model->text_buffer : "";

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str(canvas, 2, 8, model->header);
    elements_slightly_rounded_frame(canvas, 1, 12, 126, 15);

    if(canvas_string_width(canvas, text) > needed_string_width) {
        canvas_draw_str(canvas, start_pos, 22, "...");
        start_pos += 6;
        needed_string_width -= 8;
    }

    while(text && canvas_string_width(canvas, text) > needed_string_width) {
        text++;
    }

    if(model->clear_default_text) {
        elements_slightly_rounded_box(
            canvas, start_pos - 1, 14, canvas_string_width(canvas, text) + 2, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_str(canvas, start_pos + canvas_string_width(canvas, text) + 1, 22, "|");
        canvas_draw_str(canvas, start_pos + canvas_string_width(canvas, text) + 2, 22, "|");
    }
    canvas_draw_str(canvas, start_pos, 22, text);

    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < keyboard_row_count; row++) {
        const uint8_t column_count = get_row_size(row);
        const WifiPasswordInputKey* keys = get_row(row);
        for(uint8_t column = 0; column < column_count; column++) {
            wifi_password_input_draw_key(canvas, model, &keys[column], row, column);
        }
    }

    if(model->validator_message_visible) {
        canvas_set_font(canvas, FontSecondary);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 8, 10, 110, 48);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(canvas, 10, 14, &I_WarningDolphin_45x42);
        canvas_draw_rframe(canvas, 8, 8, 112, 50, 3);
        canvas_draw_rframe(canvas, 9, 9, 110, 48, 2);
        elements_multiline_text(canvas, 62, 20, furi_string_get_cstr(model->validator_text));
        canvas_set_font(canvas, FontKeyboard);
    }
}

static void wifi_password_input_handle_up(WifiPasswordInputModel* model) {
    if(model->selected_row > 0) {
        uint8_t previous_size = get_row_size(model->selected_row);
        model->selected_row--;
        if(model->selected_column >= get_row_size(model->selected_row)) {
            model->selected_column = get_row_size(model->selected_row) - 1;
        } else if(previous_size > get_row_size(model->selected_row) && model->selected_column > 0) {
            model->selected_column--;
        }
    }
}

static void wifi_password_input_handle_down(WifiPasswordInputModel* model) {
    if(model->selected_row + 1 < keyboard_row_count) {
        uint8_t previous_size = get_row_size(model->selected_row);
        model->selected_row++;
        if(model->selected_column >= get_row_size(model->selected_row)) {
            model->selected_column = get_row_size(model->selected_row) - 1;
        } else if(previous_size > get_row_size(model->selected_row) && model->selected_column > 0) {
            model->selected_column--;
        }
    }
}

static void wifi_password_input_handle_left(WifiPasswordInputModel* model) {
    if(model->selected_column > 0) {
        model->selected_column--;
    } else {
        model->selected_column = get_row_size(model->selected_row) - 1;
    }
}

static void wifi_password_input_handle_right(WifiPasswordInputModel* model) {
    if(model->selected_column + 1 < get_row_size(model->selected_row)) {
        model->selected_column++;
    } else {
        model->selected_column = 0;
    }
}

static void wifi_password_input_handle_ok(
    WifiPasswordInput* input,
    WifiPasswordInputModel* model,
    bool shift) {
    char selected = get_selected_char(model);
    size_t text_length = model->text_buffer ? strlen(model->text_buffer) : 0;

    if(!model->text_buffer || model->text_buffer_size == 0) {
        return;
    }

    if(selected == ENTER_KEY) {
        if(model->validator_callback &&
           !model->validator_callback(
               model->text_buffer, model->validator_text, model->validator_callback_context)) {
            model->validator_message_visible = true;
            furi_timer_start(input->timer, furi_kernel_get_tick_frequency() * 4);
        } else if(model->callback && text_length >= model->minimum_length) {
            model->callback(model->callback_context);
        }
    } else if(selected == BACKSPACE_KEY) {
        wifi_password_input_backspace_cb(model);
    } else {
        if(model->clear_default_text) {
            text_length = 0;
        }
        if(text_length + 1 < model->text_buffer_size) {
            model->text_buffer[text_length] = get_output_char(selected, shift);
            model->text_buffer[text_length + 1] = '\0';
        }
    }

    model->clear_default_text = false;
}

static bool wifi_password_input_view_input_callback(InputEvent* event, void* context) {
    WifiPasswordInput* input = context;
    furi_assert(input);

    bool consumed = false;
    WifiPasswordInputModel* model = view_get_model(input->view);

    if((!(event->type == InputTypePress) && !(event->type == InputTypeRelease)) &&
       model->validator_message_visible) {
        model->validator_message_visible = false;
        consumed = true;
    } else if(event->type == InputTypeShort) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            wifi_password_input_handle_up(model);
            break;
        case InputKeyDown:
            wifi_password_input_handle_down(model);
            break;
        case InputKeyLeft:
            wifi_password_input_handle_left(model);
            break;
        case InputKeyRight:
            wifi_password_input_handle_right(model);
            break;
        case InputKeyOk:
            wifi_password_input_handle_ok(input, model, false);
            break;
        default:
            consumed = false;
            break;
        }
    } else if(event->type == InputTypeLong) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            wifi_password_input_handle_up(model);
            break;
        case InputKeyDown:
            wifi_password_input_handle_down(model);
            break;
        case InputKeyLeft:
            wifi_password_input_handle_left(model);
            break;
        case InputKeyRight:
            wifi_password_input_handle_right(model);
            break;
        case InputKeyOk:
            wifi_password_input_handle_ok(input, model, true);
            break;
        case InputKeyBack:
            wifi_password_input_backspace_cb(model);
            break;
        default:
            consumed = false;
            break;
        }
    } else if(event->type == InputTypeRepeat) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            wifi_password_input_handle_up(model);
            break;
        case InputKeyDown:
            wifi_password_input_handle_down(model);
            break;
        case InputKeyLeft:
            wifi_password_input_handle_left(model);
            break;
        case InputKeyRight:
            wifi_password_input_handle_right(model);
            break;
        case InputKeyBack:
            wifi_password_input_backspace_cb(model);
            break;
        default:
            consumed = false;
            break;
        }
    }

    view_commit_model(input->view, consumed);
    return consumed;
}

static void wifi_password_input_timer_callback(void* context) {
    furi_assert(context);
    WifiPasswordInput* input = context;

    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { model->validator_message_visible = false; },
        true);
}

WifiPasswordInput* wifi_password_input_alloc(void) {
    WifiPasswordInput* input = malloc(sizeof(WifiPasswordInput));
    input->view = view_alloc();
    view_set_context(input->view, input);
    view_allocate_model(input->view, ViewModelTypeLocking, sizeof(WifiPasswordInputModel));
    view_set_draw_callback(input->view, wifi_password_input_view_draw_callback);
    view_set_input_callback(input->view, wifi_password_input_view_input_callback);
    input->timer = furi_timer_alloc(wifi_password_input_timer_callback, FuriTimerTypeOnce, input);

    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { model->validator_text = furi_string_alloc(); },
        false);

    wifi_password_input_reset(input);
    return input;
}

void wifi_password_input_free(WifiPasswordInput* input) {
    furi_check(input);
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { furi_string_free(model->validator_text); },
        false);

    furi_timer_stop(input->timer);
    furi_timer_free(input->timer);
    view_free(input->view);
    free(input);
}

void wifi_password_input_reset(WifiPasswordInput* input) {
    furi_check(input);
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        {
            model->header = "";
            model->selected_row = 0;
            model->selected_column = 0;
            model->minimum_length = 1;
            model->clear_default_text = false;
            model->text_buffer = NULL;
            model->text_buffer_size = 0;
            model->callback = NULL;
            model->callback_context = NULL;
            model->validator_callback = NULL;
            model->validator_callback_context = NULL;
            furi_string_reset(model->validator_text);
            model->validator_message_visible = false;
        },
        true);
}

View* wifi_password_input_get_view(WifiPasswordInput* input) {
    furi_check(input);
    return input->view;
}

void wifi_password_input_set_result_callback(
    WifiPasswordInput* input,
    WifiPasswordInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text) {
    furi_check(input);
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        {
            model->callback = callback;
            model->callback_context = callback_context;
            model->text_buffer = text_buffer;
            model->text_buffer_size = text_buffer_size;
            model->clear_default_text = clear_default_text;
            if(text_buffer && text_buffer[0] != '\0') {
                model->selected_row = 2;
                model->selected_column = COUNT_OF(keyboard_keys_row_3) - 1;
            }
        },
        true);
}

void wifi_password_input_set_minimum_length(WifiPasswordInput* input, size_t minimum_length) {
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { model->minimum_length = minimum_length; },
        true);
}

void wifi_password_input_set_validator(
    WifiPasswordInput* input,
    WifiPasswordInputValidatorCallback callback,
    void* callback_context) {
    furi_check(input);
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        {
            model->validator_callback = callback;
            model->validator_callback_context = callback_context;
        },
        true);
}

WifiPasswordInputValidatorCallback wifi_password_input_get_validator_callback(WifiPasswordInput* input) {
    furi_check(input);
    WifiPasswordInputValidatorCallback callback = NULL;
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { callback = model->validator_callback; },
        false);
    return callback;
}

void* wifi_password_input_get_validator_callback_context(WifiPasswordInput* input) {
    furi_check(input);
    void* callback_context = NULL;
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { callback_context = model->validator_callback_context; },
        false);
    return callback_context;
}

void wifi_password_input_set_header_text(WifiPasswordInput* input, const char* text) {
    furi_check(input);
    with_view_model(input->view, WifiPasswordInputModel * model, { model->header = text; }, true);
}
