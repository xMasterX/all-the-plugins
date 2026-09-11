#include "../nfc_magic_app_i.h"
#include "../magic/protocols/iso15693/iso15693_info.h"
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
        iso15693_info_cat_uid(uid_str, instance->iso15693_target_uid, Iso15693UidFormatGrouped);
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
        // The gen3 line is the only warning that names the COST; the title above it warns too, but
        // only about scope. The wipe performs NO magic detection -- menu, confirm, sweep -- so
        // this cannot say "your card is gen3", only what a gen3 card would cost, and @0x6r1an0y
        // (who wrote proxmark's V3 support) reports that cost is the card itself, permanently.
        // #255 is where a pre-flight probe would go; until then a static line is the whole of the
        // mitigation and the CHANGELOG only reaches release notes.
        //
        // No warning GLYPH, and this is not an oversight: FontSecondary is u8g2_font_haxrcorp4089_tr,
        // whose _r suffix is u8g2's restricted set -- ASCII 32-127 only, so an emoji renders as a
        // missing glyph. The smallest warning icon available is Warning_30x23, which is two text lines
        // tall and 30 of the 128px wide; it would force an indent and cost a line. Hence "!".
        //
        // Hard line breaks: elements_text_box wraps on its own, and left to itself it split "including"
        // mid-word.
        //
        // WHAT BINDS IS PIXEL WIDTH, NOT CHARACTER COUNT. FontSecondary is u8g2_font_haxrcorp4089_tr,
        // which has no `m` in its suffix and so is PROPORTIONAL -- `profont11_mr` (FontKeyboard) is the
        // monospace one. A line of narrow glyphs therefore fits far more than a line of wide ones, and
        // the three lines below run to 29, 23 and 27 characters against the ~24 an average mix allows.
        // Measured on device rather than counted: canvas_string_width is the only honest answer, and a
        // character budget in a comment is how a usable line gets rejected as too long.
        //
        // THREE lines is the budget, and that part IS fixed -- the font advances 11px regardless of
        // glyph, so they land at y=13/24/35 and a fourth reaches the button box at rows 52-63.
        // text_height is 38 for the same reason: at the default 54 the box runs to y=67, past the
        // screen, so an over-long string would draw UNDER the button instead of clipping.
        //
        // The title is set HERE and not from is_wipe, which also covers the USCUID-UL wipe: that card
        // is neither gen1 nor gen2, and in this app those words name the MIFARE Classic protocols.
        title = "Wipe? (gen1/gen2 only)";
        text_height = 38;
        text =
            "Zeroes every block, including\ngen1 magic 56/57/62/63.\nThis can \e#brick\e# a gen3 card!";
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
