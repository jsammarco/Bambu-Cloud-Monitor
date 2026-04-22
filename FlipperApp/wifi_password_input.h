#pragma once

#include <gui/view.h>
#include <gui/modules/validators.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WifiPasswordInput WifiPasswordInput;
typedef void (*WifiPasswordInputCallback)(void* context);
typedef bool (*WifiPasswordInputValidatorCallback)(const char* text, FuriString* error, void* context);

WifiPasswordInput* wifi_password_input_alloc(void);
void wifi_password_input_free(WifiPasswordInput* input);
void wifi_password_input_reset(WifiPasswordInput* input);
View* wifi_password_input_get_view(WifiPasswordInput* input);

void wifi_password_input_set_result_callback(
    WifiPasswordInput* input,
    WifiPasswordInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text);

void wifi_password_input_set_minimum_length(WifiPasswordInput* input, size_t minimum_length);

void wifi_password_input_set_validator(
    WifiPasswordInput* input,
    WifiPasswordInputValidatorCallback callback,
    void* callback_context);

WifiPasswordInputValidatorCallback wifi_password_input_get_validator_callback(WifiPasswordInput* input);
void* wifi_password_input_get_validator_callback_context(WifiPasswordInput* input);
void wifi_password_input_set_header_text(WifiPasswordInput* input, const char* text);

#ifdef __cplusplus
}
#endif
