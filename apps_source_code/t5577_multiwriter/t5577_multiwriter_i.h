#pragma once

#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/view.h>
#include "assets/assets_icons.h"
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <notification/notification_messages.h>

#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/widget.h>

#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <toolbox/protocols/protocol_dict.h>
#include <lfrfid/lfrfid_dict_file.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <lfrfid/tools/t5577.h>

#include "protocols/EM41XX.h"
#include "t5577_mw_listener.h"
#include "views/t5577_mw_work_view.h"
#include "views/t5577_mw_tally_view.h"
#include "scenes/t5577_multiwriter_scene.h"

#define LFRFID_APP_FOLDER             ANY_PATH("lfrfid")
#define LFRFID_APP_FILENAME_EXTENSION ".rfid"

/** Bytes a T5577 password occupies, which is one block. */
#define T5577_MW_PASSWORD_SIZE 4

/** Characters the success popup's header has room for, in the empty top right of the dolphin.
 *
 * Really 53px - x=75 to the screen edge - which is about nine characters of an ordinary word.
 * Not a buffer limit, and not a clip: elements_get_max_chars_to_fit() measures to the screen
 * edge and knows nothing of the icon, so a longer header is cut with a dash and its tail drawn
 * on a second line at y=21, straight over the dolphin's head. show_success() checks it so that
 * is caught here and not on a photograph.
 */
#define T5577_MW_POPUP_HEADER_MAX 9

enum LfRfidCustomEvent {
    LfRfidEventNext = 100,
    LfRfidEventPopupClosed,
    /** The stepped-work timer fired: run one step of the operation in progress. */
    LfRfidEventStep,
};

/** What the stepped-work scenes are addressing the tag for. */
typedef enum {
    T5577MwProtectLock,
    T5577MwProtectUnlock,
} T5577MwProtectOp;

/** Blocks one write touches and reads back: block 0 and the slot's two. */
#define T5577_MW_WRITE_BLOCKS (1 + EM41XX_BLOCKS)

/** What a check found in one key slot.
 *
 * Blank and unknown are worth telling apart by what the slot holds: zeroes are a slot nothing
 * was ever written to, anything else is somebody's data that may be worth recovering. Either
 * one is reported differently again when block 0 still broadcasts it, because then the tag is
 * sending it to every reader.
 */
typedef enum {
    T5577MwSlotUnread, // the tag stopped answering before this slot was read
    T5577MwSlotBlank, // read back as zeroes, so nothing was ever written here
    T5577MwSlotUnknown, // read back, but the parities say it is not an EM4100 frame
    T5577MwSlotKey, // holds a key
} T5577MwSlotState;

typedef struct LfRfid LfRfid;

struct LfRfid {
    ViewDispatcher* view_dispatcher;
    Gui* gui;
    NotificationApp* notifications;
    SceneManager* scene_manager;
    Storage* storage;
    DialogsApp* dialogs;
    Widget* widget;

    FuriString* file_path;
    ProtocolDict* dict;

    /** Drives the stepped work, so the dispatcher redraws and takes input between steps.
     *
     * A read or a verify holds the thread for well over a second, and doing several of them
     * inside one on_enter() is what freezes the whole GUI service rather than just this app.
     */
    FuriTimer* step_timer;
    uint8_t step;

    /** The key slot a write targets. 0 based, chosen from the start menu. */
    uint8_t slot;
    /** The key the selected file holds, waiting to be written. */
    uint64_t key;

    /** The session password, and how much is known about it.
     *
     * `set` means a word has been typed; `confirmed` means a tag answered a read addressed with
     * it and said, through its own block 0, that it wanted one. Only a confirmed word is sent
     * without asking again, and even then only after a plain detect has failed - see
     * t5577_mw_tag_detect().
     */
    uint32_t password;
    bool password_set;
    bool password_confirmed;
    /** The next detect may send the word although no tag has confirmed it.
     *
     * Set only by the explicit "try this password" action, which carries the warning, and
     * spent by the check that follows it.
     */
    bool password_try;
    uint8_t password_bytes[T5577_MW_PASSWORD_SIZE];

    /** How the tag reads, from the last detect.
     *
     * Everything else about the tag is derived from its block 0 rather than copied out of it;
     * see the accessors in t5577_mw_tag.h.
     */
    T5577Config config;

    /** What a check found in each slot, whether or not block 0 broadcasts it. */
    uint64_t slot_key[EM41XX_MAX_KEYS];
    T5577MwSlotState slot_state[EM41XX_MAX_KEYS];

    /** How the blocks of the last write read back: block 0, then the slot's two. */
    T5577BlockResult write_result[T5577_MW_WRITE_BLOCKS];
    /** And what each of them actually came back holding, when it answered at all.
     *
     * Kept because "it is not what we sent" and "it still holds what it held" are different
     * answers with different consequences, and only the word itself tells them apart.
     */
    uint32_t write_actual[T5577_MW_WRITE_BLOCKS];
    /** The words the last write sent: block 0, then the slot's two.
     *
     * Stored rather than recomposed at verification time. Recomposing block 0 reads the tag's
     * protection bits back out of the very block being checked, which compares those bits
     * against themselves and takes them out of the test altogether; the key words are stored
     * beside it because they are the same question.
     */
    uint32_t write_block0;
    uint32_t write_high;
    uint32_t write_low;
    /** Whether the last write got as far as putting blocks on the wire.
     *
     * A tag that was never found and a tag that stopped answering after it was written are
     * both a silent tag by the time the result screen runs, and only one of them has left
     * the tag half-written.
     */
    bool tag_written;

    T5577MwProtectOp protect_op;
    /** How the protect walk's checked blocks came back: block 7, then block 0.
     *
     * Its own array rather than write_result's, which is indexed by block 0 and the key slot's
     * two and means nothing here.
     */
    T5577BlockResult protect_result[2];
    /** The lock walk has written the password bit to block 0.
     *
     * From this point on a tag that stops answering may be a tag that is now protected with
     * the session password, which is the one state where forgetting that word loses the tag.
     */
    bool protect_armed;

    /** What the success popup is drawing.
     *
     * Owned here rather than left with the caller because Popup stores the pointer it is given
     * and goes on drawing from it for the length of its timeout - unlike Widget, which copies.
     * A caller's stack buffer would be read long after it had gone.
     */
    char popup_header[T5577_MW_POPUP_HEADER_MAX + 1];

    // Common Views
    Submenu* submenu;
    Popup* popup;
    ByteInput* byte_input;
    T5577MwWorkView* work_view;
    T5577MwTallyView* tally_view;

    /** Counts EM4100 frames straight off the air; see t5577_mw_listener.h for why the
     *  LF-RFID worker cannot be used for this. */
    T5577MwListener* listener;
};

typedef enum {
    LfRfidViewSubmenu,
    LfRfidViewPopup,
    LfRfidViewWidget,
    LfRfidViewByteInput,
    LfRfidViewWork,
    LfRfidViewTally,
} LfRfidView;

bool t5577_multiwriter_load_key_from_file_select(LfRfid* app);

void t5577_multiwriter_popup_timeout_callback(void* context);

void t5577_multiwriter_widget_callback(GuiButtonType result, InputType type, void* context);

/** Run the next step of a stepped operation after `delay_ms`. */
void t5577_multiwriter_step_later(LfRfid* app, uint32_t delay_ms);

void t5577_multiwriter_step_stop(LfRfid* app);

/** Drop the session password. The tag keeps whatever protection it has. */
void t5577_multiwriter_forget_password(LfRfid* app);

/** Ask the user a yes/no question, blocking until they answer. */
bool t5577_multiwriter_ask(LfRfid* app, const char* header, const char* text, const char* yes);

void t5577_multiwriter_say(LfRfid* app, const char* header, const char* text);

/** The screen every failed write, lock and unlock ends on. */
void t5577_multiwriter_show_error(LfRfid* app, const char* header, const char* text);

/** And the one they end on when they worked.
 *
 * `header` is all of it, in the empty top right of the dolphin artwork. There is deliberately
 * no detail line: a write that was not read back does not reach this screen - it ends on
 * show_error() naming the block that refused - so "Written!" already says it was verified, and
 * a second line saying so again is the only thing the strip beside the icon could hold.
 *
 * It is copied, so a caller's stack buffer is fine, and truncated to T5577_MW_POPUP_HEADER_MAX
 * rather than allowed to run across the dolphin. Overrunning it is a programmer error a debug
 * build asserts on.
 */
void t5577_multiwriter_show_success(LfRfid* app, const char* header);

/** Handle Back and a closing popup on a result screen by returning to the start menu.
 *
 * Result screens are pushed on top of the walk that produced them, so going back one scene
 * would re-enter that walk and address the tag again.
 *
 * @return     true when the event was a leaving one and the switch happened
 */
bool t5577_multiwriter_leave_to_start(LfRfid* app, SceneManagerEvent event);
