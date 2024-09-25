#pragma once

#include <gui/view.h>
#include "custom_validators.h"
#include "cli_gui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Text input anonymous structure */
typedef struct Custom_TextInput Custom_TextInput;
typedef void (*Custom_TextInputCallback)(void* context);
typedef bool (
    *Custom_TextInputValidatorCallback)(const char* text, FuriString* error, void* context);

/** Allocate and initialize text input 
 * 
 * This text input is used to enter string
 *
 * @return     Custom_TextInput instance
 */
Custom_TextInput* custom_text_input_alloc();

/** Deinitialize and free text input
 *
 * @param      text_input  Custom_TextInput instance
 */
void custom_text_input_free(Custom_TextInput* text_input);

/** Clean text input view Note: this function does not free memory
 *
 * @param      text_input  Text input instance
 */
void custom_text_input_reset(Custom_TextInput* text_input);

/** Get text input view
 *
 * @param      text_input  Custom_TextInput instance
 *
 * @return     View instance that can be used for embedding
 */
View* custom_text_input_get_view(Custom_TextInput* text_input);

/** Set text input result callback
 *
 * @param      text_input          Custom_TextInput instance
 * @param      callback            callback fn
 * @param      callback_context    callback context
 * @param      text_buffer         pointer to YOUR text buffer, that we going
 *                                 to modify
 * @param      text_buffer_size    YOUR text buffer size in bytes. Max string
 *                                 length will be text_buffer_size-1.
 * @param      clear_default_text  clear text from text_buffer on first OK
 *                                 event
 */
void custom_text_input_set_result_callback(
    Custom_TextInput* text_input,
    Custom_TextInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text);

void custom_text_input_set_validator(
    Custom_TextInput* text_input,
    Custom_TextInputValidatorCallback callback,
    void* callback_context);

void custom_text_input_set_minimum_length(Custom_TextInput* text_input, size_t minimum_length);

Custom_TextInputValidatorCallback
    custom_text_input_get_validator_callback(Custom_TextInput* text_input);

void* custom_text_input_get_validator_callback_context(Custom_TextInput* text_input);

/** Set text input header text
 *
 * @param      text_input  Custom_TextInput instance
 * @param      text        text to be shown
 */
void custom_text_input_set_header_text(Custom_TextInput* text_input, const char* text);

#ifdef __cplusplus
}
#endif
