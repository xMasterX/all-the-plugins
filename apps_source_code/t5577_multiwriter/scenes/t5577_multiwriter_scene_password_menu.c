#include "../t5577_multiwriter_i.h"

typedef enum {
    SubmenuIndexPassword,
    SubmenuIndexLock,
    SubmenuIndexUnlock,
} SubmenuIndex;

static void t5577_multiwriter_scene_password_menu_callback(void* context, uint32_t index) {
    LfRfid* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

// Rebuilt rather than navigated away from and back to, because forgetting the password changes
// both the header and the first entry and on_enter does not run again on its own.
static void t5577_multiwriter_scene_password_menu_build(LfRfid* app) {
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);

    // The header is the whole of the state: whether a word is held, and whether any tag has
    // ever answered to it. An untried word is a guess, and sending a guess is the one thing
    // here that can damage a tag.
    const char* header = "No password set";
    if(app->password_set) {
        header = app->password_confirmed ? "Password confirmed" : "Password untried";
    }
    submenu_set_header(submenu, header);

    submenu_add_item(
        submenu,
        app->password_set ? "Forget password" : "Enter password",
        SubmenuIndexPassword,
        t5577_multiwriter_scene_password_menu_callback,
        app);
    submenu_add_item(
        submenu, "Lock tag", SubmenuIndexLock, t5577_multiwriter_scene_password_menu_callback, app);
    submenu_add_item(
        submenu,
        "Unlock tag",
        SubmenuIndexUnlock,
        t5577_multiwriter_scene_password_menu_callback,
        app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, LfRfidScenePasswordMenu));
}

void t5577_multiwriter_scene_password_menu_on_enter(void* context) {
    LfRfid* app = context;

    t5577_multiwriter_scene_password_menu_build(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, LfRfidViewSubmenu);
}

// Locking installs the word in block 7; unlocking addresses the tag with it. Neither is worth
// starting without one.
static bool t5577_multiwriter_scene_password_menu_have(LfRfid* app) {
    if(app->password_set) return true;

    t5577_multiwriter_say(
        app, "No password", "Enter one first - it is\nthe word the tag will\nask for");

    return false;
}

// Unlocking has to address a tag that will not answer without the word, so it sends a password
// frame before anything has confirmed that word. That frame is the same length as a write, so a
// tag that turns out to want no password takes it into block 0 instead - which is exactly what
// happens to a tag this decoder cannot read at all, such as an HID or Indala clone on a T5577.
// Locking needs no such warning: it sends no password frame until block 7 is proven.
static bool t5577_multiwriter_scene_password_menu_may_unlock(LfRfid* app) {
    if(app->password_confirmed) return true;

    return t5577_multiwriter_ask(
        app,
        "Untried password",
        "No tag has answered to\nit. If this one has none,\nblock 0 is overwritten",
        "Unlock");
}

bool t5577_multiwriter_scene_password_menu_on_event(void* context, SceneManagerEvent event) {
    LfRfid* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, LfRfidScenePasswordMenu, event.event);

    switch(event.event) {
    case SubmenuIndexPassword:
        if(!app->password_set) {
            scene_manager_set_scene_state(app->scene_manager, LfRfidScenePasswordInput, 0);
            scene_manager_next_scene(app->scene_manager, LfRfidScenePasswordInput);
        } else if(t5577_multiwriter_ask(
                      app,
                      "Forget password?",
                      "The tag keeps its lock.\nOnly Flipper forgets,\nand cannot write it",
                      "Forget")) {
            t5577_multiwriter_forget_password(app);
            t5577_multiwriter_scene_password_menu_build(app);
        }
        return true;

    case SubmenuIndexLock:
        if(t5577_multiwriter_scene_password_menu_have(app)) {
            app->protect_op = T5577MwProtectLock;
            scene_manager_next_scene(app->scene_manager, LfRfidSceneProtect);
        }
        return true;

    case SubmenuIndexUnlock:
        if(t5577_multiwriter_scene_password_menu_have(app) &&
           t5577_multiwriter_scene_password_menu_may_unlock(app)) {
            app->protect_op = T5577MwProtectUnlock;
            scene_manager_next_scene(app->scene_manager, LfRfidSceneProtect);
        }
        return true;

    default:
        return false;
    }
}

void t5577_multiwriter_scene_password_menu_on_exit(void* context) {
    LfRfid* app = context;

    submenu_reset(app->submenu);
}
