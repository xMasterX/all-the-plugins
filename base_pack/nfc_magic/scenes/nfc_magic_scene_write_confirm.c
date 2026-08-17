#include "../nfc_magic_app_i.h"
#include <lib/nfc/protocols/iso15693_3/iso15693_3.h>

void nfc_magic_scene_write_confirm_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    NfcMagicApp* instance = context;

    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(instance->view_dispatcher, result);
    }
}

void nfc_magic_scene_write_confirm_on_enter(void* context) {
    NfcMagicApp* instance = context;
    Widget* widget = instance->widget;

    const bool iso15693 = (instance->protocol == NfcMagicProtocolIso15693);
    const bool iso15693_wipe = iso15693 && instance->iso15693_mode == NfcMagicIso15693ModeWipe;
    const bool iso15693_write_uid = iso15693 &&
                                    instance->iso15693_mode == NfcMagicIso15693ModeWriteUid;
    const bool is_wipe = instance->uscuid_ul_is_wipe_mode || iso15693_wipe;

    // Write UID is the one variant whose body is computed rather than constant. The widget takes a copy
    // of the string it is given, so this is freed before the view switch.
    FuriString* uid_str = furi_string_alloc();

    const char* title = is_wipe ? "Wipe card?" : "Risky operation";
    const char* confirm_label = "Continue";
    uint8_t text_height = 54;

    const char* text;
    if(iso15693_write_uid) {
        // 8 spaced bytes overflow the 128px width and wrap, which would push the warning off-screen,
        // so show the UID compactly (two 4-byte groups) on one line. Keep the whole box to 3 short
        // lines (UID + 2 warning): a 4th line gets squashed into the ~38px the buttons leave below
        // the title.
        for(size_t i = 0; i < ISO15693_3_UID_SIZE; ++i) {
            furi_string_cat_printf(uid_str, "%02X", instance->iso15693_target_uid[i]);
            if(i == 3) furi_string_push_back(uid_str, ' ');
        }
        furi_string_cat_str(uid_str, "\nOnly magic ISO15693\ntags accept this.");
        title = "Write UID?";
        confirm_label = "Write";
        text_height = 38;
        text = furi_string_get_cstr(uid_str);
    } else if(iso15693_wipe) {
        // "Every" is literal: 56/57/62/63 are cleared too, because on a gen2 card they are ordinary
        // user data and sparing them would leave data behind on the common card. On gen1 those same
        // blocks are the backdoor registers, so the string does not promise the UID survives -- the
        // wipe re-reads the UID afterwards and reports a change instead of claiming one. See the open
        // question in iso15693_poller_wipe_blocks.
        //
        // Hard line breaks: elements_text_box wraps on its own, and left to itself it split "including"
        // mid-word. Each line is <= 24 characters, the widest that fits this 128px box.
        text = "Zeroes every data block,\nincluding the gen1 magic\nblocks 56/57/62/63.";
    } else if(instance->uscuid_ul_is_wipe_mode) {
        text = "Blank factory dump: config &\npassword cleared, UID zeroed.";
    } else {
        text =
            "Writing to this card will change manufacturer block. On some cards it may not be rewritten";
    }

    widget_add_string_element(widget, 3, 0, AlignLeft, AlignTop, FontPrimary, title);
    widget_add_text_box_element(widget, 0, 13, 128, text_height, AlignLeft, AlignTop, text, false);
    widget_add_button_element(
        widget,
        GuiButtonTypeCenter,
        confirm_label,
        nfc_magic_scene_write_confirm_widget_callback,
        instance);
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Back", nfc_magic_scene_write_confirm_widget_callback, instance);

    furi_string_free(uid_str);

    // Setup and start worker
    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcMagicAppViewWidget);
}

bool nfc_magic_scene_write_confirm_on_event(void* context, SceneManagerEvent event) {
    NfcMagicApp* instance = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeLeft) {
            consumed = scene_manager_previous_scene(instance->scene_manager);
        } else if(event.event == GuiButtonTypeCenter) {
            scene_manager_next_scene(instance->scene_manager, NfcMagicSceneWrite);
            consumed = true;
        }
    }
    return consumed;
}

void nfc_magic_scene_write_confirm_on_exit(void* context) {
    NfcMagicApp* instance = context;

    widget_reset(instance->widget);
}
