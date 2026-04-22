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
    const WifiPasswordInputKey* rows[3];
    uint8_t keyboard_index;
} WifiPasswordKeyboard;

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
    uint8_t selected_keyboard;
    bool cursor_select;
    size_t cursor_pos;

    WifiPasswordInputValidatorCallback validator_callback;
    void* validator_callback_context;
    FuriString* validator_text;
    bool validator_message_visible;
} WifiPasswordInputModel;

static const uint8_t keyboard_origin_x = 1;
static const uint8_t keyboard_origin_y = 29;
static const uint8_t keyboard_row_count = 3;
static const uint8_t keyboard_count = 3;

#define ENTER_KEY           '\r'
#define BACKSPACE_KEY       '\b'
#define SWITCH_KEYBOARD_KEY '\t'

static const WifiPasswordInputKey keyboard_keys_row_1[] = {
    {'q', 1, 8},   {'w', 10, 8},  {'e', 19, 8},  {'r', 28, 8},  {'t', 37, 8},
    {'y', 46, 8},  {'u', 55, 8},  {'i', 64, 8},  {'o', 73, 8},  {'p', 82, 8},
    {'0', 92, 8},  {'1', 102, 8}, {'2', 111, 8}, {'3', 120, 8},
};

static const WifiPasswordInputKey keyboard_keys_row_2[] = {
    {'a', 1, 20}, {'s', 10, 20}, {'d', 19, 20}, {'f', 28, 20}, {'g', 37, 20},
    {'h', 46, 20}, {'j', 55, 20}, {'k', 64, 20}, {'l', 73, 20}, {BACKSPACE_KEY, 82, 11},
    {'4', 102, 20}, {'5', 111, 20}, {'6', 120, 20},
};

static const WifiPasswordInputKey keyboard_keys_row_3[] = {
    {SWITCH_KEYBOARD_KEY, 0, 23}, {'z', 13, 32}, {'x', 21, 32}, {'c', 29, 32},
    {'v', 37, 32}, {'b', 45, 32}, {'n', 53, 32}, {'m', 61, 32}, {'_', 69, 32},
    {ENTER_KEY, 77, 23}, {'7', 102, 32}, {'8', 111, 32}, {'9', 120, 32},
};

static const WifiPasswordInputKey symbol_keyboard_keys_row_1[] = {
    {'!', 2, 8},   {'@', 12, 8},  {'#', 22, 8},  {'$', 32, 8},  {'%', 42, 8},
    {'^', 52, 8},  {'&', 62, 8},  {'(', 71, 8},  {')', 81, 8},  {'0', 92, 8},
    {'1', 102, 8}, {'2', 111, 8}, {'3', 120, 8},
};

static const WifiPasswordInputKey symbol_keyboard_keys_row_2[] = {
    {'~', 2, 20},   {'+', 12, 20},  {'-', 22, 20},  {'=', 32, 20},
    {'[', 42, 20},  {']', 52, 20},  {'{', 62, 20},  {'}', 72, 20},
    {BACKSPACE_KEY, 82, 11}, {'4', 102, 20}, {'5', 111, 20}, {'6', 120, 20},
};

static const WifiPasswordInputKey symbol_keyboard_keys_row_3[] = {
    {SWITCH_KEYBOARD_KEY, 0, 23}, {'.', 15, 32}, {',', 29, 32}, {';', 41, 32},
    {'`', 53, 32}, {'\'', 65, 32}, {ENTER_KEY, 77, 23}, {'7', 102, 32},
    {'8', 111, 32}, {'9', 120, 32},
};

static const WifiPasswordInputKey extra_keyboard_keys_row_1[] = {
    {'*', 2, 8},   {'?', 12, 8},  {'"', 22, 8},  {':', 32, 8},  {'/', 42, 8},
    {'\\', 52, 8}, {'|', 62, 8},  {'<', 72, 8},  {'>', 82, 8},  {'0', 92, 8},
    {'1', 102, 8}, {'2', 111, 8}, {'3', 120, 8},
};

static const WifiPasswordInputKey extra_keyboard_keys_row_2[] = {
    {'_', 2, 20}, {'-', 12, 20}, {'.', 22, 20}, {',', 32, 20}, {'@', 42, 20},
    {'!', 52, 20}, {'&', 62, 20}, {'+', 72, 20}, {BACKSPACE_KEY, 82, 11},
    {'4', 102, 20}, {'5', 111, 20}, {'6', 120, 20},
};

static const WifiPasswordInputKey extra_keyboard_keys_row_3[] = {
    {SWITCH_KEYBOARD_KEY, 0, 23}, {'[', 15, 32}, {']', 29, 32}, {'(', 41, 32},
    {')', 53, 32}, {'=', 65, 32}, {ENTER_KEY, 77, 23}, {'7', 102, 32},
    {'8', 111, 32}, {'9', 120, 32},
};

static const WifiPasswordKeyboard keyboard_alpha = {
    .rows = {keyboard_keys_row_1, keyboard_keys_row_2, keyboard_keys_row_3},
    .keyboard_index = 0,
};

static const WifiPasswordKeyboard keyboard_symbols = {
    .rows = {symbol_keyboard_keys_row_1, symbol_keyboard_keys_row_2, symbol_keyboard_keys_row_3},
    .keyboard_index = 1,
};

static const WifiPasswordKeyboard keyboard_extra = {
    .rows = {extra_keyboard_keys_row_1, extra_keyboard_keys_row_2, extra_keyboard_keys_row_3},
    .keyboard_index = 2,
};

static const WifiPasswordKeyboard* keyboards[] = {
    &keyboard_alpha,
    &keyboard_symbols,
    &keyboard_extra,
};

static uint8_t get_row_size(const WifiPasswordKeyboard* keyboard, uint8_t row_index);
static const WifiPasswordInputKey* get_row(const WifiPasswordKeyboard* keyboard, uint8_t row_index);

static const WifiPasswordKeyboard* wifi_password_input_current_keyboard(
    const WifiPasswordInputModel* model) {
    return keyboards[model->selected_keyboard % keyboard_count];
}

static void wifi_password_input_switch_keyboard(WifiPasswordInputModel* model) {
    const WifiPasswordKeyboard* keyboard = NULL;
    uint8_t row_size = 0;

    model->selected_keyboard = (model->selected_keyboard + 1U) % keyboard_count;
    keyboard = wifi_password_input_current_keyboard(model);
    if(model->selected_row >= keyboard_row_count) {
        model->selected_row = 0;
    }

    row_size = get_row_size(keyboard, model->selected_row);
    if(model->selected_column >= row_size) {
        model->selected_column = row_size - 1U;
    }
}

static uint8_t get_row_size(const WifiPasswordKeyboard* keyboard, uint8_t row_index) {
    if(keyboard == &keyboard_symbols) {
        switch(row_index) {
        case 0:
            return COUNT_OF(symbol_keyboard_keys_row_1);
        case 1:
            return COUNT_OF(symbol_keyboard_keys_row_2);
        case 2:
            return COUNT_OF(symbol_keyboard_keys_row_3);
        default:
            furi_crash();
        }
    } else if(keyboard == &keyboard_extra) {
        switch(row_index) {
        case 0:
            return COUNT_OF(extra_keyboard_keys_row_1);
        case 1:
            return COUNT_OF(extra_keyboard_keys_row_2);
        case 2:
            return COUNT_OF(extra_keyboard_keys_row_3);
        default:
            furi_crash();
        }
    } else {
        switch(row_index) {
        case 0:
            return COUNT_OF(keyboard_keys_row_1);
        case 1:
            return COUNT_OF(keyboard_keys_row_2);
        case 2:
            return COUNT_OF(keyboard_keys_row_3);
        default:
            furi_crash();
        }
    }
}

static const WifiPasswordInputKey* get_row(const WifiPasswordKeyboard* keyboard, uint8_t row_index) {
    if(row_index >= keyboard_row_count) {
        furi_crash();
    }

    return keyboard->rows[row_index];
}

static char get_selected_char(WifiPasswordInputModel* model) {
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);
    return get_row(keyboard, model->selected_row)[model->selected_column].text;
}

static bool char_is_lowercase(char letter) {
    return letter >= 'a' && letter <= 'z';
}

static char char_to_uppercase(char letter) {
    if(letter == '_') {
        return ' ';
    } else if(char_is_lowercase(letter)) {
        return letter - 0x20;
    } else {
        return letter;
    }
}

static void wifi_password_input_backspace_cb(WifiPasswordInputModel* model) {
    if(!model->text_buffer || model->text_buffer_size == 0) {
        return;
    }

    if(model->clear_default_text) {
        model->text_buffer[0] = '\0';
        model->cursor_pos = 0;
        return;
    }

    if(model->cursor_pos > 0) {
        char* move = model->text_buffer + model->cursor_pos;
        memmove(move - 1, move, strlen(move) + 1);
        model->cursor_pos--;
    }
}

static void wifi_password_input_draw_key(
    Canvas* canvas,
    const WifiPasswordInputModel* model,
    const WifiPasswordInputKey* key,
    uint8_t row,
    uint8_t column,
    bool uppercase) {
    const bool selected =
        !model->cursor_select && model->selected_row == row && model->selected_column == column;
    const Icon* icon = NULL;
    char glyph = key->text;

    if(key->text == ENTER_KEY) {
        icon = selected ? &I_KeySaveSelected_22x11 : &I_KeySave_22x11;
    } else if(key->text == SWITCH_KEYBOARD_KEY) {
        icon = selected ? &I_KeyKeyboardSelected_10x11 : &I_KeyKeyboard_10x11;
    } else if(key->text == BACKSPACE_KEY) {
        icon = selected ? &I_KeyBackspaceSelected_17x11 : &I_KeyBackspace_17x11;
    }

    canvas_set_color(canvas, ColorBlack);
    if(icon) {
        canvas_draw_icon(canvas, keyboard_origin_x + key->x, keyboard_origin_y + key->y, icon);
        return;
    }

    if(selected) {
        elements_slightly_rounded_box(
            canvas, keyboard_origin_x + key->x - 2, keyboard_origin_y + key->y - 9, 9, 11);
        canvas_set_color(canvas, ColorWhite);
    }

    if(uppercase && model->selected_keyboard == keyboard_alpha.keyboard_index) {
        glyph = char_to_uppercase(glyph);
    }

    canvas_draw_glyph(
        canvas,
        keyboard_origin_x + key->x,
        keyboard_origin_y + key->y - (glyph == '_' || char_is_lowercase(glyph)),
        glyph);
}

static void wifi_password_input_view_draw_callback(Canvas* canvas, void* _model) {
    WifiPasswordInputModel* model = _model;
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);
    uint8_t text_length = model->text_buffer ? strlen(model->text_buffer) : 0;
    const bool uppercase = model->clear_default_text || text_length == 0;
    char page_label[8];
    uint8_t needed_string_width = canvas_width(canvas) - 8;
    uint8_t start_pos = 4;
    size_t cursor_pos = 0;
    char buf[text_length + 2];
    char* text = buf;

    snprintf(page_label, sizeof(page_label), "%u/%u", (unsigned)(model->selected_keyboard + 1U), (unsigned)keyboard_count);

    model->cursor_pos = model->cursor_pos > text_length ? text_length : model->cursor_pos;
    cursor_pos = model->cursor_pos;
    buf[0] = '\0';
    if(model->text_buffer) {
        strlcpy(buf, model->text_buffer, sizeof(buf));
    }

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str(canvas, 2, 8, model->header);
    canvas_draw_str(canvas, 108, 8, page_label);
    elements_slightly_rounded_frame(canvas, 1, 12, 126, 15);

    if(model->clear_default_text) {
        elements_slightly_rounded_box(
            canvas, start_pos - 1, 14, canvas_string_width(canvas, text) + 2, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        char* move = text + cursor_pos;
        memmove(move + 1, move, strlen(move) + 1);
        text[cursor_pos] = '|';
    }

    if(cursor_pos > 0 && canvas_string_width(canvas, text) > needed_string_width) {
        canvas_draw_str(canvas, start_pos, 22, "...");
        start_pos += 6;
        needed_string_width -= 8;
        for(uint32_t off = 0;
            strlen(text) && canvas_string_width(canvas, text) > needed_string_width &&
            off < cursor_pos;
            off++) {
            text++;
        }
    }

    if(canvas_string_width(canvas, text) > needed_string_width) {
        needed_string_width -= 4;
        size_t len = strlen(text);
        while(len && canvas_string_width(canvas, text) > needed_string_width) {
            text[len--] = '\0';
        }
        strlcat(text, "...", sizeof(buf) - (text - buf));
    }
    canvas_draw_str(canvas, start_pos, 22, text);

    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < keyboard_row_count; row++) {
        const uint8_t column_count = get_row_size(keyboard, row);
        const WifiPasswordInputKey* keys = get_row(keyboard, row);
        for(uint8_t column = 0; column < column_count; column++) {
            wifi_password_input_draw_key(canvas, model, &keys[column], row, column, uppercase);
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
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);

    if(model->selected_row > 0) {
        model->selected_row--;
        if(model->selected_column >= get_row_size(keyboard, model->selected_row)) {
            model->selected_column = get_row_size(keyboard, model->selected_row) - 1;
        }
    } else {
        model->cursor_select = true;
        model->clear_default_text = false;
    }
}

static void wifi_password_input_handle_down(WifiPasswordInputModel* model) {
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);

    if(model->cursor_select) {
        model->cursor_select = false;
    } else if(model->selected_row + 1 < keyboard_row_count) {
        model->selected_row++;
        if(model->selected_column >= get_row_size(keyboard, model->selected_row)) {
            model->selected_column = get_row_size(keyboard, model->selected_row) - 1;
        }
    }
}

static void wifi_password_input_handle_left(WifiPasswordInputModel* model) {
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);

    if(model->cursor_select) {
        model->clear_default_text = false;
        if(model->cursor_pos > 0) {
            model->cursor_pos = CLAMP(model->cursor_pos - 1, strlen(model->text_buffer), 0u);
        }
    } else if(model->selected_column > 0) {
        model->selected_column--;
    } else {
        model->selected_column = get_row_size(keyboard, model->selected_row) - 1;
    }
}

static void wifi_password_input_handle_right(WifiPasswordInputModel* model) {
    const WifiPasswordKeyboard* keyboard = wifi_password_input_current_keyboard(model);

    if(model->cursor_select) {
        model->clear_default_text = false;
        model->cursor_pos = CLAMP(model->cursor_pos + 1, strlen(model->text_buffer), 0u);
    } else if(model->selected_column + 1 < get_row_size(keyboard, model->selected_row)) {
        model->selected_column++;
    } else {
        model->selected_column = 0;
    }
}

static void wifi_password_input_handle_ok(
    WifiPasswordInput* input,
    WifiPasswordInputModel* model,
    InputType type) {
    char selected = '\0';
    size_t text_length = 0;

    if(!model->text_buffer || model->text_buffer_size == 0) {
        return;
    }

    if(model->cursor_select) {
        model->clear_default_text = !model->clear_default_text;
        return;
    }

    selected = get_selected_char(model);
    text_length = strlen(model->text_buffer);

    if(selected == ENTER_KEY) {
        if(model->validator_callback &&
           !model->validator_callback(
               model->text_buffer, model->validator_text, model->validator_callback_context)) {
            model->validator_message_visible = true;
            furi_timer_start(input->timer, furi_kernel_get_tick_frequency() * 4);
        } else if(model->callback && text_length >= model->minimum_length) {
            model->callback(model->callback_context);
        }
    } else if(selected == SWITCH_KEYBOARD_KEY) {
        wifi_password_input_switch_keyboard(model);
        if(model->selected_column >=
           get_row_size(wifi_password_input_current_keyboard(model), model->selected_row)) {
            model->selected_column = 0;
        }
    } else if(selected == BACKSPACE_KEY) {
        wifi_password_input_backspace_cb(model);
    } else if(type != InputTypeRepeat) {
        if(model->clear_default_text) {
            text_length = 0;
        }

        if(text_length < model->text_buffer_size - 1U) {
            if((type == InputTypeLong) != (text_length == 0) &&
               model->selected_keyboard == keyboard_alpha.keyboard_index) {
                selected = char_to_uppercase(selected);
            }

            if(model->clear_default_text) {
                model->text_buffer[0] = selected;
                model->text_buffer[1] = '\0';
                model->cursor_pos = 1;
            } else {
                char* move = model->text_buffer + model->cursor_pos;
                memmove(move + 1, move, strlen(move) + 1);
                model->text_buffer[model->cursor_pos] = selected;
                model->cursor_pos++;
            }
        }
    }

    model->clear_default_text = false;
}

static bool wifi_password_input_view_input_callback(InputEvent* event, void* context) {
    WifiPasswordInput* input = context;
    WifiPasswordInputModel* model = NULL;
    bool consumed = false;

    furi_assert(input);
    model = view_get_model(input->view);

    if((event->type != InputTypePress && event->type != InputTypeRelease) &&
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
            wifi_password_input_handle_ok(input, model, event->type);
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
            wifi_password_input_handle_ok(input, model, event->type);
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
    WifiPasswordInput* input = context;

    furi_assert(input);
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
            model->selected_keyboard = 0;
            model->cursor_select = false;
            model->cursor_pos = 0;
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
            model->selected_keyboard = 0;
            model->selected_row = 0;
            model->selected_column = 0;
            model->cursor_select = false;
            model->cursor_pos = text_buffer ? strlen(text_buffer) : 0;
            if(text_buffer && text_buffer[0] != '\0') {
                model->selected_row = 2;
                model->selected_column = 9;
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
    WifiPasswordInputValidatorCallback callback = NULL;

    furi_check(input);
    with_view_model(
        input->view,
        WifiPasswordInputModel * model,
        { callback = model->validator_callback; },
        false);
    return callback;
}

void* wifi_password_input_get_validator_callback_context(WifiPasswordInput* input) {
    void* callback_context = NULL;

    furi_check(input);
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
