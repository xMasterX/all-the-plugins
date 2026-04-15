#pragma once

#include <gui/view.h>
#include "fznote_validators.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Text input anonymous structure */
typedef struct FzNoteTextInput FzNoteTextInput;
typedef void (*FzNoteTextInputCallback)(void* context);
typedef bool (
    *FzNoteTextInputValidatorCallback)(const char* text, FuriString* error, void* context);

/** Allocate and initialize text input 
 * 
 * This text input is used to enter string
 *
 * @return     FzNoteTextInput instance
 */
FzNoteTextInput* fznote_text_input_alloc();

/** Deinitialize and free text input
 *
 * @param      text_input  FzNoteTextInput instance
 */
void fznote_text_input_free(FzNoteTextInput* text_input);

/** Clean text input view Note: this function does not free memory
 *
 * @param      text_input  Text input instance
 */
void fznote_text_input_reset(FzNoteTextInput* text_input);

/** Get text input view
 *
 * @param      text_input  FzNoteTextInput instance
 *
 * @return     View instance that can be used for embedding
 */
View* fznote_text_input_get_view(FzNoteTextInput* text_input);

/** Set text input result callback
 *
 * @param      text_input          FzNoteTextInput instance
 * @param      callback            callback fn
 * @param      callback_context    callback context
 * @param      text_buffer         pointer to YOUR text buffer, that we going
 *                                 to modify
 * @param      text_buffer_size    YOUR text buffer size in bytes. Max string
 *                                 length will be text_buffer_size-1.
 * @param      clear_default_text  clear text from text_buffer on first OK
 *                                 event
 */
void fznote_text_input_set_result_callback(
    FzNoteTextInput* text_input,
    FzNoteTextInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text);

void fznote_text_input_set_validator(
    FzNoteTextInput* text_input,
    FzNoteTextInputValidatorCallback callback,
    void* callback_context);

void fznote_text_input_set_minimum_length(FzNoteTextInput* text_input, size_t minimum_length);

FzNoteTextInputValidatorCallback
    fznote_text_input_get_validator_callback(FzNoteTextInput* text_input);

void* fznote_text_input_get_validator_callback_context(FzNoteTextInput* text_input);

/** Set text input header text
 *
 * @param      text_input  FzNoteTextInput instance
 * @param      text        text to be shown
 */
void fznote_text_input_set_header_text(FzNoteTextInput* text_input, const char* text);

#ifdef __cplusplus
}
#endif
