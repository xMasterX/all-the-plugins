#include <furi.h>

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/widget.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <dialogs/dialogs.h>

#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <strings.h>

#define TAG             "dbc_app"
#define TEXT_INPUT_SIZE 32
#define MAX_SIGNALS     32

#define CANTOOLS_EVENT_TEXT_INPUT_SAVE   1U
#define CANTOOLS_EVENT_SORT_TOGGLE       2U
#define CANTOOLS_EVENT_BYTE_INPUT_SAVE   3U
#define CANTOOLS_EVENT_DECODE_MANUAL     4U
#define CANTOOLS_EVENT_DECODE_LOG        5U
#define CANTOOLS_EVENT_DECODE_CANID_SAVE 6U
#define CANTOOLS_EVENT_DECODE_DLC_SAVE   7U
#define CANTOOLS_EVENT_SIGNAL_BASE       100U

// Folder + file format for saved DBC signals (may change to .dbc later)
#define DBC_USER_PATH    EXT_PATH("apps_data/dbc_signals")
#define DBC_EXTENSION    ".txt"
#define DBC_FILETYPE     "DBC_SIGNAL"
#define DBC_FILE_VERSION 1

typedef enum {
    CantoolsScenesMainMenuScene,
    CantoolsScenesDbcMakerMenuScene,
    CantoolsScenesSignalNameScene,
    CantoolsScenesCanIdScene,
    CantoolsScenesStartBitScene,
    CantoolsScenesBitLengthScene,
    CantoolsScenesEndiannessScene,
    CantoolsScenesSignedScene,
    CantoolsScenesOffsetScene,
    CantoolsScenesScalarScene,
    CantoolsScenesUnitScene,
    CantoolsScenesMinScene,
    CantoolsScenesMaxScene,
    CantoolsScenesCalculateScene,
    CantoolsScenesViewDbcListScene,
    CantoolsScenesSignalActionsScene,
    CantoolsScenesViewDbcDetailScene,
    CantoolsScenesDecodeDataScene,
    CantoolsScenesDecodeManualCanIdScene,
    CantoolsScenesDecodeManualDlcScene,
    CantoolsScenesDecodeManualDataScene,
    CantoolsScenesDecodeResultScene,
    CantoolsScenesDecodeLogScene,
    CantoolsScenesSceneCount,
} CantoolsScenesScene;

typedef enum {
    CantoolsScenesSubmenuView,
    CantoolsScenesWidgetView,
    CantoolsScenesTextInputView,
    CantoolsScenesByteInputView,
} CantoolsScenesView;

typedef enum {
    CantoolsSortByName,
    CantoolsSortByCanId,
} CantoolsSortMode;

typedef enum {
    DbcFieldSignalName,
    DbcFieldCanId,
    DbcFieldStartBit,
    DbcFieldBitLength,
    DbcFieldEndianness,
    DbcFieldSignedness,
    DbcFieldOffset,
    DbcFieldScalar,
    DbcFieldUnit,
    DbcFieldMin,
    DbcFieldMax,
    DbcFieldCount,
} DbcFieldId;

// Typical DBC signal components
typedef struct {
    bool used;
    char signal_name[TEXT_INPUT_SIZE];
    char can_id[TEXT_INPUT_SIZE];
    char start_bit[TEXT_INPUT_SIZE];
    char bit_length[TEXT_INPUT_SIZE];
    char endianness[TEXT_INPUT_SIZE];
    char signedness[TEXT_INPUT_SIZE];
    char offset[TEXT_INPUT_SIZE];
    char scalar[TEXT_INPUT_SIZE];
    char unit[TEXT_INPUT_SIZE];
    char min_val[TEXT_INPUT_SIZE];
    char max_val[TEXT_INPUT_SIZE];
    char file_path[256];
} DbcSignal;

typedef struct App {
    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    TextInput* text_input;
    ByteInput* byte_input;
    DialogsApp* dialogs;
    char* buffer; // For text input
    uint8_t buffer_size; // for text input
    DbcFieldId active_field;
    CantoolsSortMode sort_mode;
    bool editing;
    char editing_file_path[256];
    char decode_can_id_text[TEXT_INPUT_SIZE];
    char decode_dlc_text[TEXT_INPUT_SIZE];
    uint8_t decode_data_bytes[8];
    uint32_t decode_can_id;
    uint8_t decode_dlc;
    uint8_t decode_data_len;

    // Current-in-edit signal fields
    char signal_name[TEXT_INPUT_SIZE];
    char can_id[TEXT_INPUT_SIZE];
    char start_bit[TEXT_INPUT_SIZE];
    char bit_length[TEXT_INPUT_SIZE];
    char endianness[TEXT_INPUT_SIZE];
    char signedness[TEXT_INPUT_SIZE];
    char offset[TEXT_INPUT_SIZE];
    char scalar[TEXT_INPUT_SIZE];
    char unit[TEXT_INPUT_SIZE];
    char min_val[TEXT_INPUT_SIZE];
    char max_val[TEXT_INPUT_SIZE];

    // Saved signals (loaded from SD + new ones)
    DbcSignal signals[MAX_SIGNALS];
    uint8_t signal_count;
    int selected_signal_index;
} App;

typedef enum {
    CantoolsScenesMainMenuSceneDbcMaker,
    CantoolsScenesMainMenuSceneViewDbc,
    CantoolsScenesMainMenuSceneDecodeData,
} CantoolsScenesMainMenuSceneIndex;

typedef enum {
    CantoolsScenesMainMenuSceneDbcMakerEvent,
    CantoolsScenesMainMenuSceneViewDbcEvent,
    CantoolsScenesMainMenuSceneDecodeDataEvent,
} CantoolsScenesMainMenuEvent;

typedef enum {
    CantoolsDbcMakerMenuGenerate = 1000,
    CantoolsDbcMakerMenuResetDefaults,
} CantoolsDbcMakerMenuAction;

typedef enum {
    CantoolsSignalActionView,
    CantoolsSignalActionEdit,
    CantoolsSignalActionDelete,
} CantoolsSignalAction;

// Substitute "_" characters with "." for decimal input support
void decimal_substitute(char* s) {
    if(!s) return;
    for(size_t i = 0; s[i]; i++) {
        if(s[i] == '_') {
            s[i] = '.';
            FURI_LOG_D(TAG, "Substituted '.' for '_' in: %s", s);
        }
    }
}

typedef bool (*DbcFieldValidator)(App* app, const char* value, FuriString* error);

typedef struct {
    const char* menu_label;
    const char* input_header;
    size_t offset;
    CantoolsScenesScene scene;
    CantoolsScenesScene next_scene;
    DbcFieldValidator validator;
} DbcFieldConfig;

static bool validate_required(const char* value, const char* label, FuriString* error) {
    if(!value || value[0] == '\0') {
        furi_string_printf(error, "%s is required.", label);
        return false;
    }
    return true;
}

static bool parse_uint32_str(const char* value, uint32_t* out) {
    if(!value || value[0] == '\0') return false;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 0);
    if(end == value || *end != '\0') return false;
    if(parsed > UINT32_MAX) return false;
    if(out) *out = (uint32_t)parsed;
    return true;
}

static bool parse_int32_str(const char* value, int32_t* out) {
    if(!value || value[0] == '\0') return false;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if(end == value || *end != '\0') return false;
    if(parsed > INT32_MAX || parsed < INT32_MIN) return false;
    if(out) *out = (int32_t)parsed;
    return true;
}

static bool parse_float_str(const char* value) {
    if(!value || value[0] == '\0') return false;
    char tmp[TEXT_INPUT_SIZE];
    strlcpy(tmp, value, sizeof(tmp));
    decimal_substitute(tmp);
    char* end = NULL;
    strtod(tmp, &end);
    return (end != tmp && *end == '\0');
}

static bool validate_signal_name(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    return validate_required(value, "Signal name", error);
}

static bool validate_can_id(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    if(!validate_required(value, "CAN ID", error)) return false;
    uint32_t can_id = 0;
    if(!parse_uint32_str(value, &can_id) || can_id > 0x1FFFFFFF) {
        furi_string_set(error, "CAN ID must be 0x0..0x1FFFFFFF.");
        return false;
    }
    return true;
}

static bool validate_start_bit(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    int32_t start_bit = 0;
    if(!validate_required(value, "Start bit", error)) return false;
    if(!parse_int32_str(value, &start_bit) || start_bit < 0 || start_bit > 63) {
        furi_string_set(error, "Start bit must be 0..63.");
        return false;
    }
    return true;
}

static bool validate_bit_length(App* app, const char* value, FuriString* error) {
    int32_t bit_length = 0;
    int32_t start_bit = 0;
    if(!validate_required(value, "Bit length", error)) return false;
    if(!parse_int32_str(value, &bit_length) || bit_length < 1 || bit_length > 64) {
        furi_string_set(error, "Bit length must be 1..64.");
        return false;
    }
    if(parse_int32_str(app->start_bit, &start_bit)) {
        if(start_bit + bit_length > 64) {
            furi_string_set(error, "Start bit + length must be <= 64.");
            return false;
        }
    }
    return true;
}

static bool validate_endianness(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    if(!validate_required(value, "Endianness", error)) return false;
    char c = value[0];
    if(!(c == '0' || c == '1' || c == 'l' || c == 'L' || c == 'b' || c == 'B' || c == 'm' ||
         c == 'M')) {
        furi_string_set(error, "Use 0/1, l/b (little/big).");
        return false;
    }
    return true;
}

static bool validate_signedness(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    if(!validate_required(value, "Signedness", error)) return false;
    char c = value[0];
    if(!(c == 's' || c == 'S' || c == 'u' || c == 'U' || c == '+' || c == '-' || c == '0' ||
         c == '1')) {
        furi_string_set(error, "Use s/u or +/-.");
        return false;
    }
    return true;
}

static bool validate_optional_number(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    if(!value || value[0] == '\0') return true;
    if(!parse_float_str(value)) {
        furi_string_set(error, "Enter a valid number.");
        return false;
    }
    return true;
}

static bool validate_optional_text(App* app, const char* value, FuriString* error) {
    UNUSED(app);
    UNUSED(value);
    UNUSED(error);
    return true;
}

static const DbcFieldConfig dbc_field_configs[DbcFieldCount] = {
    [DbcFieldSignalName] =
        {
            .menu_label = "Signal name",
            .input_header = "Signal name:",
            .offset = offsetof(App, signal_name),
            .scene = CantoolsScenesSignalNameScene,
            .next_scene = CantoolsScenesCanIdScene,
            .validator = validate_signal_name,
        },
    [DbcFieldCanId] =
        {
            .menu_label = "CAN ID",
            .input_header = "CAN ID (dec/0x):",
            .offset = offsetof(App, can_id),
            .scene = CantoolsScenesCanIdScene,
            .next_scene = CantoolsScenesStartBitScene,
            .validator = validate_can_id,
        },
    [DbcFieldStartBit] =
        {
            .menu_label = "Start bit",
            .input_header = "Start bit (0-63):",
            .offset = offsetof(App, start_bit),
            .scene = CantoolsScenesStartBitScene,
            .next_scene = CantoolsScenesBitLengthScene,
            .validator = validate_start_bit,
        },
    [DbcFieldBitLength] =
        {
            .menu_label = "Bit length",
            .input_header = "Bit length (1-64):",
            .offset = offsetof(App, bit_length),
            .scene = CantoolsScenesBitLengthScene,
            .next_scene = CantoolsScenesEndiannessScene,
            .validator = validate_bit_length,
        },
    [DbcFieldEndianness] =
        {
            .menu_label = "Endianness",
            .input_header = "Endianness (0=LE/1=BE):",
            .offset = offsetof(App, endianness),
            .scene = CantoolsScenesEndiannessScene,
            .next_scene = CantoolsScenesSignedScene,
            .validator = validate_endianness,
        },
    [DbcFieldSignedness] =
        {
            .menu_label = "Signed?",
            .input_header = "Signed? (s/u):",
            .offset = offsetof(App, signedness),
            .scene = CantoolsScenesSignedScene,
            .next_scene = CantoolsScenesOffsetScene,
            .validator = validate_signedness,
        },
    [DbcFieldOffset] =
        {
            .menu_label = "Offset",
            .input_header = "Offset (use '_' for .):",
            .offset = offsetof(App, offset),
            .scene = CantoolsScenesOffsetScene,
            .next_scene = CantoolsScenesScalarScene,
            .validator = validate_optional_number,
        },
    [DbcFieldScalar] =
        {
            .menu_label = "Scalar",
            .input_header = "Scalar (use '_' for .):",
            .offset = offsetof(App, scalar),
            .scene = CantoolsScenesScalarScene,
            .next_scene = CantoolsScenesUnitScene,
            .validator = validate_optional_number,
        },
    [DbcFieldUnit] =
        {
            .menu_label = "Unit",
            .input_header = "Unit (e.g. kph):",
            .offset = offsetof(App, unit),
            .scene = CantoolsScenesUnitScene,
            .next_scene = CantoolsScenesMinScene,
            .validator = validate_optional_text,
        },
    [DbcFieldMin] =
        {
            .menu_label = "Min",
            .input_header = "Min (use '_' for .):",
            .offset = offsetof(App, min_val),
            .scene = CantoolsScenesMinScene,
            .next_scene = CantoolsScenesMaxScene,
            .validator = validate_optional_number,
        },
    [DbcFieldMax] =
        {
            .menu_label = "Max",
            .input_header = "Max (use '_' for .):",
            .offset = offsetof(App, max_val),
            .scene = CantoolsScenesMaxScene,
            .next_scene = CantoolsScenesCalculateScene,
            .validator = validate_optional_number,
        },
};

static char* dbc_field_ptr(App* app, DbcFieldId field_id) {
    return (char*)((uint8_t*)app + dbc_field_configs[field_id].offset);
}

static void cantools_show_message(App* app, const char* header, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 24, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

// Normalize scalar/offset/min/max fields
void calculate_values(void* context) {
    App* app = context;

    decimal_substitute(app->scalar);
    decimal_substitute(app->offset);
    decimal_substitute(app->min_val);
    decimal_substitute(app->max_val);

    FURI_LOG_D(TAG, "Normalized scalar: %s", app->scalar);
    FURI_LOG_D(TAG, "Normalized offset: %s", app->offset);
    FURI_LOG_D(TAG, "Normalized min: %s", app->min_val);
    FURI_LOG_D(TAG, "Normalized max: %s", app->max_val);
}

static void sanitize_filename(FuriString* s) {
    FuriString* clean = furi_string_alloc();
    const char* src = furi_string_get_cstr(s);
    for(size_t i = 0; src[i] != '\0'; i++) {
        char c = src[i];
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'z') || (c >= '0' && c <= '9') ||
           (c == '_')) {
            furi_string_push_back(clean, c);
        } else {
            furi_string_push_back(clean, '_');
        }
    }
    furi_string_set(s, clean);
    furi_string_free(clean);
}

static void dbc_build_signal_path(const char* signal_name, char* out, size_t out_size) {
    FuriString* filename = furi_string_alloc();
    FuriString* path = furi_string_alloc();

    furi_string_set_str(filename, signal_name && signal_name[0] ? signal_name : "Signal");
    sanitize_filename(filename);
    furi_string_printf(
        path, "%s/%s%s", DBC_USER_PATH, furi_string_get_cstr(filename), DBC_EXTENSION);

    strlcpy(out, furi_string_get_cstr(path), out_size);

    furi_string_free(filename);
    furi_string_free(path);
}

// format a DBC entry into a FuriString from given fields
static void format_dbc_entry(
    FuriString* out,
    const char* signal_name,
    const char* can_id_str,
    const char* start_bit,
    const char* bit_length,
    const char* endianness,
    const char* signedness,
    const char* scalar,
    const char* offset,
    const char* min_val,
    const char* max_val,
    const char* unit) {
    if(!signal_name || strlen(signal_name) == 0) {
        signal_name = "Signal";
    }

    // Parse CAN ID (supports decimal or 0xHEX)
    uint32_t can_id = 0;
    if(can_id_str && strlen(can_id_str) > 0) {
        can_id = (uint32_t)strtoul(can_id_str, NULL, 0);
    }

    // Determine endianness char
    char endian_char = '0';
    if(endianness && strlen(endianness) > 0) {
        char c = endianness[0];
        if(c == '1' || c == 'M' || c == 'm' || c == 'B' || c == 'b') {
            endian_char = '1';
        } else {
            endian_char = '0';
        }
    }

    // Determine sign char
    char sign_char = '+';
    if(signedness && strlen(signedness) > 0) {
        char c = signedness[0];
        if(c == 's' || c == 'S' || c == '-' || c == '1') {
            sign_char = '-';
        } else {
            sign_char = '+';
        }
    }

    const char* msg_name = signal_name;

    const char* sb_str = (start_bit && strlen(start_bit) > 0) ? start_bit : "0";
    const char* bl_str = (bit_length && strlen(bit_length) > 0) ? bit_length : "8";
    const char* sc_str = (scalar && strlen(scalar) > 0) ? scalar : "1";
    const char* off_str = (offset && strlen(offset) > 0) ? offset : "0";
    const char* min_str = (min_val && strlen(min_val) > 0) ? min_val : "0";
    const char* max_str = (max_val && strlen(max_val) > 0) ? max_val : "0";
    const char* unit_str = (unit && strlen(unit) > 0) ? unit : "";

    furi_string_printf(
        out,
        "BO_ %lu %s: 8 VehicleBus\n"
        " SG_ %s : %s|%s@%c%c (%s,%s) [%s|%s] \"%s\"  Receiver",
        (unsigned long)can_id,
        msg_name,
        signal_name,
        sb_str,
        bl_str,
        endian_char,
        sign_char,
        sc_str,
        off_str,
        min_str,
        max_str,
        unit_str);
}

// SD / Storage Handling

static uint32_t dbc_parse_can_id(const DbcSignal* sig);
static void dbc_sort_signals(App* app);

static void dbc_init_folder(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FURI_LOG_I(TAG, "Creating DBC folder: %s", DBC_USER_PATH);
    if(storage_simply_mkdir(storage, DBC_USER_PATH)) {
        FURI_LOG_I(TAG, "DBC folder successfully created!");
    } else {
        FURI_LOG_I(TAG, "DBC folder already exists or could not be created.");
    }
    furi_record_close(RECORD_STORAGE);
}

// Save one DbcSignal to SD as a FlipperFormat file
static bool dbc_save_signal_to_sd(const DbcSignal* sig, char* saved_path, size_t saved_size) {
    if(!sig) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    char path[256];
    dbc_build_signal_path(sig->signal_name, path, sizeof(path));

    FURI_LOG_I(TAG, "Saving DBC signal to: %s", path);

    if(!flipper_format_file_open_new(ff, path)) {
        FURI_LOG_E(TAG, "Could not open new DBC file");
        goto cleanup;
    }

    if(!flipper_format_write_header_cstr(ff, DBC_FILETYPE, DBC_FILE_VERSION)) {
        FURI_LOG_E(TAG, "Could not write header");
        goto cleanup;
    }

    FuriString* tmp = furi_string_alloc();

    // Write each field as a string
    furi_string_set_str(tmp, sig->signal_name);
    if(!flipper_format_write_string(ff, "SignalName", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->can_id);
    if(!flipper_format_write_string(ff, "CanID", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->start_bit);
    if(!flipper_format_write_string(ff, "StartBit", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->bit_length);
    if(!flipper_format_write_string(ff, "BitLength", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->endianness);
    if(!flipper_format_write_string(ff, "Endianness", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->signedness);
    if(!flipper_format_write_string(ff, "Signedness", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->offset);
    if(!flipper_format_write_string(ff, "Offset", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->scalar);
    if(!flipper_format_write_string(ff, "Scalar", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->unit);
    if(!flipper_format_write_string(ff, "Unit", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->min_val);
    if(!flipper_format_write_string(ff, "Min", tmp)) goto write_fail;

    furi_string_set_str(tmp, sig->max_val);
    if(!flipper_format_write_string(ff, "Max", tmp)) goto write_fail;

    ok = true;
    goto cleanup_tmp;

write_fail:
    FURI_LOG_E(TAG, "Error writing DBC signal fields");

cleanup_tmp:
    furi_string_free(tmp);

cleanup:
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    if(ok && saved_path && saved_size > 0) {
        strlcpy(saved_path, path, saved_size);
    }
    return ok;
}

static bool dbc_load_signal_from_file(DbcSignal* sig, const char* path) {
    if(!sig || !path) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    if(!flipper_format_file_open_existing(ff, path)) {
        FURI_LOG_E(TAG, "Could not open DBC file: %s", path);
        goto cleanup;
    }

    FuriString* tmp = furi_string_alloc();

    if(!flipper_format_read_string(ff, "SignalName", tmp)) goto read_fail;
    strlcpy(sig->signal_name, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "CanID", tmp)) goto read_fail;
    strlcpy(sig->can_id, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "StartBit", tmp)) goto read_fail;
    strlcpy(sig->start_bit, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "BitLength", tmp)) goto read_fail;
    strlcpy(sig->bit_length, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Endianness", tmp)) goto read_fail;
    strlcpy(sig->endianness, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Signedness", tmp)) goto read_fail;
    strlcpy(sig->signedness, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Offset", tmp)) goto read_fail;
    strlcpy(sig->offset, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Scalar", tmp)) goto read_fail;
    strlcpy(sig->scalar, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Unit", tmp)) goto read_fail;
    strlcpy(sig->unit, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Min", tmp)) goto read_fail;
    strlcpy(sig->min_val, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    if(!flipper_format_read_string(ff, "Max", tmp)) goto read_fail;
    strlcpy(sig->max_val, furi_string_get_cstr(tmp), TEXT_INPUT_SIZE);

    sig->used = true;
    strlcpy(sig->file_path, path, sizeof(sig->file_path));
    ok = true;
    goto cleanup_tmp;

read_fail:
    FURI_LOG_E(TAG, "Error reading DBC signal fields from %s", path);

cleanup_tmp:
    furi_string_free(tmp);

cleanup:
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

// Scan the DBC_USER_PATH directory and load all signals into app->signals
static void dbc_load_all_signals(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    app->signal_count = 0;
    for(uint8_t i = 0; i < MAX_SIGNALS; i++) {
        app->signals[i].used = false;
        app->signals[i].file_path[0] = '\0';
    }

    File* dir = storage_file_alloc(storage);
    FileInfo file_info;
    char name[128];

    if(!storage_dir_open(dir, DBC_USER_PATH)) {
        FURI_LOG_W(TAG, "Could not open DBC dir: %s", DBC_USER_PATH);
        storage_file_free(dir);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    while(storage_dir_read(dir, &file_info, name, sizeof(name))) {
        //if(file_info.type != FileTypeFile) continue;

        // filter by extension
        size_t len = strlen(name);
        size_t ext_len = strlen(DBC_EXTENSION);
        if(len <= ext_len) continue;
        if(strcmp(name + len - ext_len, DBC_EXTENSION) != 0) continue;

        if(app->signal_count >= MAX_SIGNALS) {
            FURI_LOG_W(TAG, "MAX_SIGNALS reached, ignoring extra files");
            break;
        }

        char fullpath[256];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", DBC_USER_PATH, name);

        DbcSignal* sig = &app->signals[app->signal_count];
        if(dbc_load_signal_from_file(sig, fullpath)) {
            FURI_LOG_I(TAG, "Loaded DBC signal from %s", fullpath);
            app->signal_count++;
        } else {
            FURI_LOG_W(TAG, "Failed to load DBC from %s", fullpath);
        }
    }

    dbc_sort_signals(app);

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
}

static uint32_t dbc_parse_can_id(const DbcSignal* sig) {
    uint32_t can_id = 0;
    if(sig && sig->can_id[0]) {
        parse_uint32_str(sig->can_id, &can_id);
    }
    return can_id;
}

static void dbc_sort_signals(App* app) {
    if(app->signal_count < 2) return;
    for(uint8_t i = 0; i < app->signal_count - 1; i++) {
        for(uint8_t j = i + 1; j < app->signal_count; j++) {
            DbcSignal* a = &app->signals[i];
            DbcSignal* b = &app->signals[j];
            int cmp = 0;
            if(app->sort_mode == CantoolsSortByCanId) {
                uint32_t a_id = dbc_parse_can_id(a);
                uint32_t b_id = dbc_parse_can_id(b);
                if(a_id < b_id)
                    cmp = -1;
                else if(a_id > b_id)
                    cmp = 1;
                else
                    cmp = strcasecmp(a->signal_name, b->signal_name);
            } else {
                cmp = strcasecmp(a->signal_name, b->signal_name);
            }
            if(cmp > 0) {
                DbcSignal tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

// Save "current" fields from App into SD, then reload all into memory
static void dbc_save_current_and_reload(App* app) {
    DbcSignal tmp = {0};
    tmp.used = true;
    strlcpy(tmp.signal_name, app->signal_name, TEXT_INPUT_SIZE);
    strlcpy(tmp.can_id, app->can_id, TEXT_INPUT_SIZE);
    strlcpy(tmp.start_bit, app->start_bit, TEXT_INPUT_SIZE);
    strlcpy(tmp.bit_length, app->bit_length, TEXT_INPUT_SIZE);
    strlcpy(tmp.endianness, app->endianness, TEXT_INPUT_SIZE);
    strlcpy(tmp.signedness, app->signedness, TEXT_INPUT_SIZE);
    strlcpy(tmp.offset, app->offset, TEXT_INPUT_SIZE);
    strlcpy(tmp.scalar, app->scalar, TEXT_INPUT_SIZE);
    strlcpy(tmp.unit, app->unit, TEXT_INPUT_SIZE);
    strlcpy(tmp.min_val, app->min_val, TEXT_INPUT_SIZE);
    strlcpy(tmp.max_val, app->max_val, TEXT_INPUT_SIZE);
    char saved_path[256] = {0};
    if(!dbc_save_signal_to_sd(&tmp, saved_path, sizeof(saved_path))) {
        FURI_LOG_E(TAG, "Failed to save current signal to SD");
    } else if(
        app->editing && app->editing_file_path[0] != '\0' &&
        strcmp(app->editing_file_path, saved_path) != 0) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FS_Error status = storage_common_remove(storage, app->editing_file_path);
        furi_record_close(RECORD_STORAGE);
        if(status != FSE_OK && status != FSE_EXIST) {
            FURI_LOG_W(TAG, "Could not remove old signal file: %s", app->editing_file_path);
        }
    }

    app->editing = false;
    app->editing_file_path[0] = '\0';
    strlcpy(app->decode_can_id_text, "0x", sizeof(app->decode_can_id_text));
    strlcpy(app->decode_dlc_text, "8", sizeof(app->decode_dlc_text));
    memset(app->decode_data_bytes, 0, sizeof(app->decode_data_bytes));
    app->decode_can_id = 0;
    app->decode_dlc = 0;
    app->decode_data_len = 0;

    dbc_load_all_signals(app);

    // Try to set selected index to the last signal with same name
    app->selected_signal_index = -1;
    for(uint8_t i = 0; i < app->signal_count; i++) {
        if(strcmp(app->signals[i].signal_name, app->signal_name) == 0) {
            app->selected_signal_index = i;
        }
    }
}

static void dbc_copy_signal_to_app(App* app, const DbcSignal* sig) {
    strlcpy(app->signal_name, sig->signal_name, TEXT_INPUT_SIZE);
    strlcpy(app->can_id, sig->can_id, TEXT_INPUT_SIZE);
    strlcpy(app->start_bit, sig->start_bit, TEXT_INPUT_SIZE);
    strlcpy(app->bit_length, sig->bit_length, TEXT_INPUT_SIZE);
    strlcpy(app->endianness, sig->endianness, TEXT_INPUT_SIZE);
    strlcpy(app->signedness, sig->signedness, TEXT_INPUT_SIZE);
    strlcpy(app->offset, sig->offset, TEXT_INPUT_SIZE);
    strlcpy(app->scalar, sig->scalar, TEXT_INPUT_SIZE);
    strlcpy(app->unit, sig->unit, TEXT_INPUT_SIZE);
    strlcpy(app->min_val, sig->min_val, TEXT_INPUT_SIZE);
    strlcpy(app->max_val, sig->max_val, TEXT_INPUT_SIZE);
}

static void app_set_defaults(App* app) {
    strlcpy(app->signal_name, "DI_vehicleSpeed", TEXT_INPUT_SIZE);
    strlcpy(app->can_id, "0x257", TEXT_INPUT_SIZE);
    strlcpy(app->start_bit, "12", TEXT_INPUT_SIZE);
    strlcpy(app->bit_length, "12", TEXT_INPUT_SIZE);
    strlcpy(app->endianness, "1", TEXT_INPUT_SIZE);
    strlcpy(app->signedness, "u", TEXT_INPUT_SIZE);
    strlcpy(app->offset, "-40", TEXT_INPUT_SIZE);
    strlcpy(app->scalar, "0_08", TEXT_INPUT_SIZE);
    strlcpy(app->unit, "kph", TEXT_INPUT_SIZE);
    strlcpy(app->min_val, "-40", TEXT_INPUT_SIZE);
    strlcpy(app->max_val, "285", TEXT_INPUT_SIZE);
}

static void dbc_build_field_label(App* app, DbcFieldId field_id, FuriString* out) {
    const DbcFieldConfig* cfg = &dbc_field_configs[field_id];
    const char* value = dbc_field_ptr(app, field_id);
    const char* display = (value && value[0]) ? value : "<unset>";
    furi_string_printf(out, "%s: %s", cfg->menu_label, display);
}

static bool dbc_validate_field(App* app, DbcFieldId field_id, FuriString* error) {
    const DbcFieldConfig* cfg = &dbc_field_configs[field_id];
    const char* value = dbc_field_ptr(app, field_id);
    if(cfg->validator) {
        return cfg->validator(app, value, error);
    }
    return true;
}

static bool dbc_validate_all(App* app) {
    FuriString* error = furi_string_alloc();
    for(uint8_t i = 0; i < DbcFieldCount; i++) {
        if(!dbc_validate_field(app, (DbcFieldId)i, error)) {
            cantools_show_message(app, "Invalid input", furi_string_get_cstr(error));
            furi_string_free(error);
            return false;
        }
    }
    furi_string_free(error);
    return true;
}

static double parse_double_or_default(const char* value, double fallback) {
    if(!value || value[0] == '\0') return fallback;
    char tmp[TEXT_INPUT_SIZE];
    strlcpy(tmp, value, sizeof(tmp));
    decimal_substitute(tmp);
    char* end = NULL;
    double parsed = strtod(tmp, &end);
    if(end == tmp || *end != '\0') return fallback;
    return parsed;
}

static bool decode_extract_raw(
    const uint8_t* data,
    uint8_t data_len,
    uint8_t start_bit,
    uint8_t bit_length,
    bool big_endian,
    uint64_t* out_raw) {
    if(!data || !out_raw) return false;
    if(bit_length == 0 || bit_length > 64) return false;
    if(start_bit > 63) return false;
    if(!big_endian) {
        uint64_t value = 0;
        for(uint8_t i = 0; i < data_len; i++) {
            value |= ((uint64_t)data[i]) << (8U * i);
        }
        uint64_t mask = (bit_length == 64) ? UINT64_MAX : ((1ULL << bit_length) - 1ULL);
        *out_raw = (value >> start_bit) & mask;
        return true;
    }

    if(start_bit + 1 < bit_length) return false;
    uint64_t raw = 0;
    for(uint8_t i = 0; i < bit_length; i++) {
        int32_t bit_index = (int32_t)start_bit - i;
        uint8_t byte_index = (uint8_t)(bit_index / 8);
        if(byte_index >= data_len) return false;
        uint8_t bit_in_byte = 7U - (bit_index % 8);
        uint8_t bit = (data[byte_index] >> bit_in_byte) & 0x01;
        raw = (raw << 1U) | bit;
    }
    *out_raw = raw;
    return true;
}

static bool decode_signal_value(
    const DbcSignal* sig,
    const uint8_t* data,
    uint8_t data_len,
    double* out_value) {
    if(!sig || !data || !out_value) return false;
    int32_t start_bit = 0;
    int32_t bit_length = 0;
    if(!parse_int32_str(sig->start_bit, &start_bit)) return false;
    if(!parse_int32_str(sig->bit_length, &bit_length)) return false;
    if(start_bit < 0 || start_bit > 63) return false;
    if(bit_length <= 0 || bit_length > 64) return false;

    bool big_endian = false;
    if(sig->endianness[0]) {
        char c = sig->endianness[0];
        big_endian = (c == '1' || c == 'M' || c == 'm' || c == 'B' || c == 'b');
    }

    uint64_t raw = 0;
    if(!decode_extract_raw(
           data, data_len, (uint8_t)start_bit, (uint8_t)bit_length, big_endian, &raw)) {
        return false;
    }

    bool is_signed = false;
    if(sig->signedness[0]) {
        char c = sig->signedness[0];
        is_signed = (c == 's' || c == 'S' || c == '-' || c == '1');
    }

    int64_t signed_raw = (int64_t)raw;
    if(is_signed && bit_length < 64) {
        uint64_t sign_bit = 1ULL << (bit_length - 1);
        if(raw & sign_bit) {
            uint64_t mask = (1ULL << bit_length) - 1ULL;
            signed_raw = (int64_t)(raw | (~mask));
        }
    }

    double scale = parse_double_or_default(sig->scalar, 1.0);
    double offset = parse_double_or_default(sig->offset, 0.0);
    *out_value = (double)signed_raw * scale + offset;
    return true;
}

void Cantools_scenes_main_menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case CantoolsScenesMainMenuSceneDbcMaker:
        scene_manager_handle_custom_event(
            app->scene_manager, CantoolsScenesMainMenuSceneDbcMakerEvent);
        break;
    case CantoolsScenesMainMenuSceneViewDbc:
        scene_manager_handle_custom_event(
            app->scene_manager, CantoolsScenesMainMenuSceneViewDbcEvent);
        break;
    case CantoolsScenesMainMenuSceneDecodeData:
        scene_manager_handle_custom_event(
            app->scene_manager, CantoolsScenesMainMenuSceneDecodeDataEvent);
        break;
    }
}

void Cantools_scenes_main_menu_scene_on_enter(void* context) {
    App* app = context;
    app->editing = false;
    app->editing_file_path[0] = '\0';
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "CAN Tools");

    submenu_add_item(
        app->submenu,
        "DBC Maker",
        CantoolsScenesMainMenuSceneDbcMaker,
        Cantools_scenes_main_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "View DBC",
        CantoolsScenesMainMenuSceneViewDbc,
        Cantools_scenes_main_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Decode Data",
        CantoolsScenesMainMenuSceneDecodeData,
        Cantools_scenes_main_menu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesSubmenuView);
}

bool Cantools_scenes_main_menu_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    bool consumed = false;
    switch(event.type) {
    case SceneManagerEventTypeCustom:
        switch(event.event) {
        case CantoolsScenesMainMenuSceneDbcMakerEvent:
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDbcMakerMenuScene);
            consumed = true;
            break;
        case CantoolsScenesMainMenuSceneViewDbcEvent:
            // ensure we are up-to-date from SD before viewing
            dbc_load_all_signals(app);
            scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
            consumed = true;
            break;
        case CantoolsScenesMainMenuSceneDecodeDataEvent:
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeDataScene);
            consumed = true;
            break;
        }
        break;
    default:
        break;
    }
    return consumed;
}

void Cantools_scenes_main_menu_scene_on_exit(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
}

void Cantools_scenes_dbc_maker_menu_callback(void* context, uint32_t index) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, index);
}

void Cantools_scenes_dbc_maker_menu_scene_on_enter(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->editing ? "Edit Signal" : "DBC Maker");

    FuriString* label = furi_string_alloc();
    for(uint8_t i = 0; i < DbcFieldCount; i++) {
        dbc_build_field_label(app, (DbcFieldId)i, label);
        submenu_add_item(
            app->submenu,
            furi_string_get_cstr(label),
            i,
            Cantools_scenes_dbc_maker_menu_callback,
            app);
    }
    furi_string_free(label);
    submenu_add_item(
        app->submenu,
        "Generate & Save",
        CantoolsDbcMakerMenuGenerate,
        Cantools_scenes_dbc_maker_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Reset defaults",
        CantoolsDbcMakerMenuResetDefaults,
        Cantools_scenes_dbc_maker_menu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesSubmenuView);
}

bool Cantools_scenes_dbc_maker_menu_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event < DbcFieldCount) {
            DbcFieldId field_id = (DbcFieldId)event.event;
            app->active_field = field_id;
            scene_manager_next_scene(app->scene_manager, dbc_field_configs[field_id].scene);
            consumed = true;
        } else if(event.event == CantoolsDbcMakerMenuGenerate) {
            scene_manager_next_scene(app->scene_manager, CantoolsScenesCalculateScene);
            consumed = true;
        } else if(event.event == CantoolsDbcMakerMenuResetDefaults) {
            app_set_defaults(app);
            calculate_values(app);
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDbcMakerMenuScene);
            consumed = true;
        }
    }
    return consumed;
}

void Cantools_scenes_dbc_maker_menu_scene_on_exit(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
}

static void cantools_text_input_callback(void* context) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, CANTOOLS_EVENT_TEXT_INPUT_SAVE);
}

static void cantools_text_input_scene_on_enter(App* app, DbcFieldId field_id) {
    bool clear_text = true;
    char* value = dbc_field_ptr(app, field_id);
    strlcpy(app->buffer, value, app->buffer_size);
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, dbc_field_configs[field_id].input_header);
    text_input_set_result_callback(
        app->text_input,
        cantools_text_input_callback,
        app,
        app->buffer,
        app->buffer_size,
        clear_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesTextInputView);
}

static bool
    cantools_text_input_scene_on_event(App* app, SceneManagerEvent event, DbcFieldId field_id) {
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == CANTOOLS_EVENT_TEXT_INPUT_SAVE) {
        FuriString* error = furi_string_alloc();
        if(!dbc_field_configs[field_id].validator(app, app->buffer, error)) {
            cantools_show_message(app, "Invalid input", furi_string_get_cstr(error));
            furi_string_free(error);
            return true;
        }
        furi_string_free(error);
        strlcpy(dbc_field_ptr(app, field_id), app->buffer, TEXT_INPUT_SIZE);
        calculate_values(app);
        scene_manager_next_scene(app->scene_manager, dbc_field_configs[field_id].next_scene);
        return true;
    }
    return false;
}

#define DEFINE_FIELD_SCENE_HANDLERS(field_id, name)                                        \
    void Cantools_scenes_##name##_scene_on_enter(void* context) {                          \
        App* app = context;                                                                \
        cantools_text_input_scene_on_enter(app, field_id);                                 \
    }                                                                                      \
    bool Cantools_scenes_##name##_scene_on_event(void* context, SceneManagerEvent event) { \
        App* app = context;                                                                \
        return cantools_text_input_scene_on_event(app, event, field_id);                   \
    }                                                                                      \
    void Cantools_scenes_##name##_scene_on_exit(void* context) {                           \
        UNUSED(context);                                                                   \
    }

DEFINE_FIELD_SCENE_HANDLERS(DbcFieldSignalName, signal_name)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldCanId, can_id)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldStartBit, start_bit)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldBitLength, bit_length)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldEndianness, endianness)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldSignedness, signed)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldOffset, offset)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldScalar, scalar)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldUnit, unit)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldMin, min)
DEFINE_FIELD_SCENE_HANDLERS(DbcFieldMax, max)

void Cantools_scenes_calculate_scene_on_enter(void* context) {
    App* app = context;
    widget_reset(app->widget);

    if(!dbc_validate_all(app)) {
        scene_manager_next_scene(app->scene_manager, CantoolsScenesDbcMakerMenuScene);
        return;
    }

    // Normalize numeric-like fields
    calculate_values(app);

    // Persist to SD and reload in-memory list
    dbc_save_current_and_reload(app);

    FuriString* dbc = furi_string_alloc();
    format_dbc_entry(
        dbc,
        app->signal_name,
        app->can_id,
        app->start_bit,
        app->bit_length,
        app->endianness,
        app->signedness,
        app->scalar,
        app->offset,
        app->min_val,
        app->max_val,
        app->unit);

    widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, furi_string_get_cstr(dbc));

    furi_string_free(dbc);

    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
}
bool Cantools_scenes_calculate_scene_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}
void Cantools_scenes_calculate_scene_on_exit(void* context) {
    UNUSED(context);
}

void Cantools_scenes_view_dbc_menu_callback(void* context, uint32_t index) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, index);
}

void Cantools_scenes_view_dbc_list_scene_on_enter(void* context) {
    App* app = context;

    if(app->signal_count == 0) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget,
            1,
            1,
            128,
            64,
            "No signals defined.\n\nUse DBC Maker to add\nsignals first.");
        view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
    } else {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "Saved DBC signals");
        const char* sort_label = (app->sort_mode == CantoolsSortByCanId) ? "Sort: CAN ID" :
                                                                           "Sort: Name";
        submenu_add_item(
            app->submenu,
            sort_label,
            CANTOOLS_EVENT_SORT_TOGGLE,
            Cantools_scenes_view_dbc_menu_callback,
            app);

        FuriString* label = furi_string_alloc();
        for(uint8_t i = 0; i < app->signal_count; i++) {
            const char* name = app->signals[i].signal_name;
            if(!name || strlen(name) == 0) {
                name = "(unnamed)";
            }
            furi_string_printf(label, "%s (%s)", name, app->signals[i].can_id);
            submenu_add_item(
                app->submenu,
                furi_string_get_cstr(label),
                CANTOOLS_EVENT_SIGNAL_BASE + i,
                Cantools_scenes_view_dbc_menu_callback,
                app);
        }
        furi_string_free(label);
        view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesSubmenuView);
    }
}

bool Cantools_scenes_view_dbc_list_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CANTOOLS_EVENT_SORT_TOGGLE) {
            app->sort_mode = (app->sort_mode == CantoolsSortByCanId) ? CantoolsSortByName :
                                                                       CantoolsSortByCanId;
            dbc_sort_signals(app);
            scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
            return true;
        }
        if(event.event >= CANTOOLS_EVENT_SIGNAL_BASE) {
            uint32_t idx = event.event - CANTOOLS_EVENT_SIGNAL_BASE;
            if(idx < app->signal_count) {
                app->selected_signal_index = (int)idx;
                scene_manager_next_scene(app->scene_manager, CantoolsScenesSignalActionsScene);
                return true;
            }
        }
    }
    return false;
}
void Cantools_scenes_view_dbc_list_scene_on_exit(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
}

void Cantools_scenes_signal_actions_menu_callback(void* context, uint32_t index) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, index);
}

void Cantools_scenes_signal_actions_scene_on_enter(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Signal actions");
    submenu_add_item(
        app->submenu,
        "View details",
        CantoolsSignalActionView,
        Cantools_scenes_signal_actions_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Edit",
        CantoolsSignalActionEdit,
        Cantools_scenes_signal_actions_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Delete",
        CantoolsSignalActionDelete,
        Cantools_scenes_signal_actions_menu_callback,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesSubmenuView);
}

bool Cantools_scenes_signal_actions_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(app->selected_signal_index < 0 || app->selected_signal_index >= app->signal_count) {
            cantools_show_message(app, "Error", "Invalid signal selection.");
            scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
            return true;
        }
        DbcSignal* sig = &app->signals[app->selected_signal_index];
        switch(event.event) {
        case CantoolsSignalActionView:
            scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcDetailScene);
            return true;
        case CantoolsSignalActionEdit:
            dbc_copy_signal_to_app(app, sig);
            app->editing = true;
            strlcpy(app->editing_file_path, sig->file_path, sizeof(app->editing_file_path));
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDbcMakerMenuScene);
            return true;
        case CantoolsSignalActionDelete: {
            DialogMessage* message = dialog_message_alloc();
            dialog_message_set_header(message, "Delete signal?", 64, 0, AlignCenter, AlignTop);
            dialog_message_set_text(message, sig->signal_name, 64, 20, AlignCenter, AlignTop);
            dialog_message_set_buttons(message, "Cancel", NULL, "Delete");
            DialogMessageButton result = dialog_message_show(app->dialogs, message);
            dialog_message_free(message);
            if(result == DialogMessageButtonRight) {
                Storage* storage = furi_record_open(RECORD_STORAGE);
                FS_Error status = storage_common_remove(storage, sig->file_path);
                furi_record_close(RECORD_STORAGE);
                if(status == FSE_OK || status == FSE_EXIST) {
                    app->editing = false;
                    app->editing_file_path[0] = '\0';
                    dbc_load_all_signals(app);
                    scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
                } else {
                    cantools_show_message(app, "Error", "Failed to delete file.");
                    scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
                }
            } else {
                scene_manager_next_scene(app->scene_manager, CantoolsScenesViewDbcListScene);
            }
            return true;
        }
        default:
            break;
        }
    }
    return false;
}

void Cantools_scenes_signal_actions_scene_on_exit(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
}

void Cantools_scenes_view_dbc_detail_scene_on_enter(void* context) {
    App* app = context;
    widget_reset(app->widget);

    if(app->selected_signal_index < 0 || app->selected_signal_index >= app->signal_count) {
        widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, "Invalid signal index.");
    } else {
        DbcSignal* sig = &app->signals[app->selected_signal_index];
        if(!sig->used) {
            widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, "Empty signal slot.");
        } else {
            FuriString* dbc = furi_string_alloc();
            format_dbc_entry(
                dbc,
                sig->signal_name,
                sig->can_id,
                sig->start_bit,
                sig->bit_length,
                sig->endianness,
                sig->signedness,
                sig->scalar,
                sig->offset,
                sig->min_val,
                sig->max_val,
                sig->unit);
            widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, furi_string_get_cstr(dbc));
            furi_string_free(dbc);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
}
bool Cantools_scenes_view_dbc_detail_scene_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}
void Cantools_scenes_view_dbc_detail_scene_on_exit(void* context) {
    UNUSED(context);
}

// Decoding

static void cantools_byte_input_callback(void* context) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, CANTOOLS_EVENT_BYTE_INPUT_SAVE);
}

static void cantools_decode_byte_input_on_enter(
    App* app,
    const char* header,
    uint8_t* buffer,
    uint8_t size) {
    byte_input_set_header_text(app->byte_input, header);
    byte_input_set_result_callback(
        app->byte_input, cantools_byte_input_callback, NULL, app, buffer, size);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesByteInputView);
}

static void cantools_decode_canid_input_callback(void* context) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, CANTOOLS_EVENT_DECODE_CANID_SAVE);
}

static void cantools_decode_dlc_input_callback(void* context) {
    App* app = context;
    scene_manager_handle_custom_event(app->scene_manager, CANTOOLS_EVENT_DECODE_DLC_SAVE);
}

static void cantools_decode_text_input_on_enter(
    App* app,
    const char* header,
    char* value,
    TextInputCallback callback) {
    bool clear_text = true;
    strlcpy(app->buffer, value, app->buffer_size);
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, header);
    text_input_set_result_callback(
        app->text_input, callback, app, app->buffer, app->buffer_size, clear_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesTextInputView);
}

void Cantools_scenes_decode_data_scene_on_enter(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Decode Data");
    submenu_add_item(
        app->submenu,
        "Manual frame",
        CANTOOLS_EVENT_DECODE_MANUAL,
        Cantools_scenes_view_dbc_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Decode log",
        CANTOOLS_EVENT_DECODE_LOG,
        Cantools_scenes_view_dbc_menu_callback,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesSubmenuView);
}

bool Cantools_scenes_decode_data_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CANTOOLS_EVENT_DECODE_MANUAL) {
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeManualCanIdScene);
            return true;
        }
        if(event.event == CANTOOLS_EVENT_DECODE_LOG) {
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeLogScene);
            return true;
        }
    }
    return false;
}

void Cantools_scenes_decode_data_scene_on_exit(void* context) {
    App* app = context;
    submenu_reset(app->submenu);
}

void Cantools_scenes_decode_manual_can_id_scene_on_enter(void* context) {
    App* app = context;
    if(app->decode_can_id_text[0] == '\0') {
        strlcpy(app->decode_can_id_text, "0x", sizeof(app->decode_can_id_text));
    }
    cantools_decode_text_input_on_enter(
        app, "CAN ID (dec/0x):", app->decode_can_id_text, cantools_decode_canid_input_callback);
}

bool Cantools_scenes_decode_manual_can_id_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == CANTOOLS_EVENT_DECODE_CANID_SAVE) {
        strlcpy(app->decode_can_id_text, app->buffer, sizeof(app->decode_can_id_text));
        uint32_t can_id = 0;
        if(!parse_uint32_str(app->decode_can_id_text, &can_id) || can_id > 0x1FFFFFFF) {
            cantools_show_message(app, "Invalid CAN ID", "Use 0x0..0x1FFFFFFF.");
            return true;
        }
        app->decode_can_id = can_id;
        scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeManualDlcScene);
        return true;
    }
    return false;
}

void Cantools_scenes_decode_manual_can_id_scene_on_exit(void* context) {
    UNUSED(context);
}

void Cantools_scenes_decode_manual_dlc_scene_on_enter(void* context) {
    App* app = context;
    if(app->decode_dlc_text[0] == '\0') {
        strlcpy(app->decode_dlc_text, "8", sizeof(app->decode_dlc_text));
    }
    cantools_decode_text_input_on_enter(
        app, "DLC (0-8):", app->decode_dlc_text, cantools_decode_dlc_input_callback);
}

bool Cantools_scenes_decode_manual_dlc_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == CANTOOLS_EVENT_DECODE_DLC_SAVE) {
        strlcpy(app->decode_dlc_text, app->buffer, sizeof(app->decode_dlc_text));
        int32_t dlc = 0;
        if(!parse_int32_str(app->decode_dlc_text, &dlc) || dlc < 0 || dlc > 8) {
            cantools_show_message(app, "Invalid DLC", "DLC must be 0..8.");
            return true;
        }
        app->decode_dlc = (uint8_t)dlc;
        app->decode_data_len = app->decode_dlc;
        if(app->decode_dlc == 0) {
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeResultScene);
        } else {
            scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeManualDataScene);
        }
        return true;
    }
    return false;
}

void Cantools_scenes_decode_manual_dlc_scene_on_exit(void* context) {
    UNUSED(context);
}

void Cantools_scenes_decode_manual_data_scene_on_enter(void* context) {
    App* app = context;
    uint8_t len = app->decode_data_len;
    if(len == 0 || len > 8) len = 8;
    cantools_decode_byte_input_on_enter(app, "Data bytes:", app->decode_data_bytes, len);
}

bool Cantools_scenes_decode_manual_data_scene_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == CANTOOLS_EVENT_BYTE_INPUT_SAVE) {
        scene_manager_next_scene(app->scene_manager, CantoolsScenesDecodeResultScene);
        return true;
    }
    return false;
}

void Cantools_scenes_decode_manual_data_scene_on_exit(void* context) {
    UNUSED(context);
}

void Cantools_scenes_decode_result_scene_on_enter(void* context) {
    App* app = context;
    widget_reset(app->widget);

    if(app->signal_count == 0) {
        widget_add_text_scroll_element(
            app->widget, 1, 1, 128, 64, "No signals saved.\nAdd DBC signals first.");
        view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
        return;
    }

    FuriString* result = furi_string_alloc();
    furi_string_printf(
        result, "CAN ID: 0x%lX\nDLC: %u\n\n", (unsigned long)app->decode_can_id, app->decode_dlc);

    bool matched = false;
    for(uint8_t i = 0; i < app->signal_count; i++) {
        const DbcSignal* sig = &app->signals[i];
        uint32_t sig_id = dbc_parse_can_id(sig);
        if(sig_id != app->decode_can_id) continue;

        double value = 0.0;
        if(!decode_signal_value(sig, app->decode_data_bytes, app->decode_dlc, &value)) {
            furi_string_cat_printf(result, "%s: decode error\n", sig->signal_name);
            matched = true;
            continue;
        }

        const char* unit = sig->unit[0] ? sig->unit : "";
        furi_string_cat_printf(result, "%s: %.3f %s\n", sig->signal_name, value, unit);
        matched = true;
    }

    if(!matched) {
        furi_string_cat_str(result, "No matching signals.");
    }

    widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, furi_string_get_cstr(result));
    furi_string_free(result);
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
}

bool Cantools_scenes_decode_result_scene_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void Cantools_scenes_decode_result_scene_on_exit(void* context) {
    UNUSED(context);
}

void Cantools_scenes_decode_log_scene_on_enter(void* context) {
    App* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 1, 1, 128, 64, "Decode log\n\nComing soon...");
    view_dispatcher_switch_to_view(app->view_dispatcher, CantoolsScenesWidgetView);
}

bool Cantools_scenes_decode_log_scene_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void Cantools_scenes_decode_log_scene_on_exit(void* context) {
    UNUSED(context);
}

// Scene manager handlers, lots of scenes :(

void (*const Cantools_scenes_scene_on_enter_handlers[])(void*) = {
    Cantools_scenes_main_menu_scene_on_enter, // 0
    Cantools_scenes_dbc_maker_menu_scene_on_enter, // 1
    Cantools_scenes_signal_name_scene_on_enter, // 2
    Cantools_scenes_can_id_scene_on_enter, // 3
    Cantools_scenes_start_bit_scene_on_enter, // 4
    Cantools_scenes_bit_length_scene_on_enter, // 5
    Cantools_scenes_endianness_scene_on_enter, // 6
    Cantools_scenes_signed_scene_on_enter, // 7
    Cantools_scenes_offset_scene_on_enter, // 8
    Cantools_scenes_scalar_scene_on_enter, // 9
    Cantools_scenes_unit_scene_on_enter, // 10
    Cantools_scenes_min_scene_on_enter, // 11
    Cantools_scenes_max_scene_on_enter, // 12
    Cantools_scenes_calculate_scene_on_enter, // 13
    Cantools_scenes_view_dbc_list_scene_on_enter, // 14
    Cantools_scenes_signal_actions_scene_on_enter, // 15
    Cantools_scenes_view_dbc_detail_scene_on_enter, // 16
    Cantools_scenes_decode_data_scene_on_enter, // 17
    Cantools_scenes_decode_manual_can_id_scene_on_enter, // 18
    Cantools_scenes_decode_manual_dlc_scene_on_enter, // 19
    Cantools_scenes_decode_manual_data_scene_on_enter, // 20
    Cantools_scenes_decode_result_scene_on_enter, // 21
    Cantools_scenes_decode_log_scene_on_enter, // 22
};

bool (*const Cantools_scenes_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    Cantools_scenes_main_menu_scene_on_event,
    Cantools_scenes_dbc_maker_menu_scene_on_event,
    Cantools_scenes_signal_name_scene_on_event,
    Cantools_scenes_can_id_scene_on_event,
    Cantools_scenes_start_bit_scene_on_event,
    Cantools_scenes_bit_length_scene_on_event,
    Cantools_scenes_endianness_scene_on_event,
    Cantools_scenes_signed_scene_on_event,
    Cantools_scenes_offset_scene_on_event,
    Cantools_scenes_scalar_scene_on_event,
    Cantools_scenes_unit_scene_on_event,
    Cantools_scenes_min_scene_on_event,
    Cantools_scenes_max_scene_on_event,
    Cantools_scenes_calculate_scene_on_event,
    Cantools_scenes_view_dbc_list_scene_on_event,
    Cantools_scenes_signal_actions_scene_on_event,
    Cantools_scenes_view_dbc_detail_scene_on_event,
    Cantools_scenes_decode_data_scene_on_event,
    Cantools_scenes_decode_manual_can_id_scene_on_event,
    Cantools_scenes_decode_manual_dlc_scene_on_event,
    Cantools_scenes_decode_manual_data_scene_on_event,
    Cantools_scenes_decode_result_scene_on_event,
    Cantools_scenes_decode_log_scene_on_event,
};

void (*const Cantools_scenes_scene_on_exit_handlers[])(void*) = {
    Cantools_scenes_main_menu_scene_on_exit,
    Cantools_scenes_dbc_maker_menu_scene_on_exit,
    Cantools_scenes_signal_name_scene_on_exit,
    Cantools_scenes_can_id_scene_on_exit,
    Cantools_scenes_start_bit_scene_on_exit,
    Cantools_scenes_bit_length_scene_on_exit,
    Cantools_scenes_endianness_scene_on_exit,
    Cantools_scenes_signed_scene_on_exit,
    Cantools_scenes_offset_scene_on_exit,
    Cantools_scenes_scalar_scene_on_exit,
    Cantools_scenes_unit_scene_on_exit,
    Cantools_scenes_min_scene_on_exit,
    Cantools_scenes_max_scene_on_exit,
    Cantools_scenes_calculate_scene_on_exit,
    Cantools_scenes_view_dbc_list_scene_on_exit,
    Cantools_scenes_signal_actions_scene_on_exit,
    Cantools_scenes_view_dbc_detail_scene_on_exit,
    Cantools_scenes_decode_data_scene_on_exit,
    Cantools_scenes_decode_manual_can_id_scene_on_exit,
    Cantools_scenes_decode_manual_dlc_scene_on_exit,
    Cantools_scenes_decode_manual_data_scene_on_exit,
    Cantools_scenes_decode_result_scene_on_exit,
    Cantools_scenes_decode_log_scene_on_exit,
};

static const SceneManagerHandlers Cantools_scenes_scene_manager_handlers = {
    .on_enter_handlers = Cantools_scenes_scene_on_enter_handlers,
    .on_event_handlers = Cantools_scenes_scene_on_event_handlers,
    .on_exit_handlers = Cantools_scenes_scene_on_exit_handlers,
    .scene_num = CantoolsScenesSceneCount,
};

static bool cantools_is_dbc_input_scene(CantoolsScenesScene scene) {
    return (scene >= CantoolsScenesSignalNameScene && scene <= CantoolsScenesCalculateScene);
}

static bool Cantools_scene_custom_callback(void* context, uint32_t custom_event) {
    furi_assert(context);
    App* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, custom_event);
}

// Back button handler - so we don't have to keep pressing back through all DBC input scenes
bool Cantools_scene_back_event_callback(void* context) {
    furi_assert(context);
    App* app = context;
    CantoolsScenesScene current =
        (CantoolsScenesScene)scene_manager_get_current_scene(app->scene_manager);
    if(cantools_is_dbc_input_scene(current)) {
        scene_manager_search_and_switch_to_another_scene(
            app->scene_manager, CantoolsScenesDbcMakerMenuScene);
        return true;
    }
    return scene_manager_handle_back_event(app->scene_manager);
}

// App alloc / free / init

static App* app_alloc() {
    App* app = malloc(sizeof(App));
    app->buffer_size = TEXT_INPUT_SIZE;
    app->buffer = malloc(app->buffer_size);

    app->scene_manager = scene_manager_alloc(&Cantools_scenes_scene_manager_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, Cantools_scene_custom_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, Cantools_scene_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CantoolsScenesSubmenuView, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CantoolsScenesWidgetView, widget_get_view(app->widget));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CantoolsScenesTextInputView, text_input_get_view(app->text_input));

    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CantoolsScenesByteInputView, byte_input_get_view(app->byte_input));

    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->sort_mode = CantoolsSortByName;
    app->editing = false;
    app->editing_file_path[0] = '\0';

    return app;
}

static void app_free(App* app) {
    furi_assert(app);
    view_dispatcher_remove_view(app->view_dispatcher, CantoolsScenesSubmenuView);
    view_dispatcher_remove_view(app->view_dispatcher, CantoolsScenesWidgetView);
    view_dispatcher_remove_view(app->view_dispatcher, CantoolsScenesTextInputView);
    view_dispatcher_remove_view(app->view_dispatcher, CantoolsScenesByteInputView);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    submenu_free(app->submenu);
    widget_free(app->widget);
    text_input_free(app->text_input);
    byte_input_free(app->byte_input);
    furi_record_close(RECORD_DIALOGS);
    free(app->buffer);
    free(app);
}

static void app_initialize(App* app) {
    app_set_defaults(app);

    app->signal_count = 0;
    app->selected_signal_index = -1;
    app->editing = false;
    app->editing_file_path[0] = '\0';
    for(int i = 0; i < MAX_SIGNALS; i++) {
        app->signals[i].used = false;
        app->signals[i].file_path[0] = '\0';
    }

    calculate_values(app);

    dbc_init_folder();
    dbc_load_all_signals(app);
}

int32_t cantools_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    app_initialize(app);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    scene_manager_next_scene(app->scene_manager, CantoolsScenesMainMenuScene);
    view_dispatcher_run(app->view_dispatcher);

    app_free(app);
    return 0;
}
