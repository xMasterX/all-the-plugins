#include "src/views/cfs_screen.h"

#include <furi.h>
#include <gui/elements.h>

#define CFS_SCREEN_MENU_MAX (12)
#define CFS_SCREEN_ANIM_MS  (180)

typedef enum {
    CfsScreenModeNone,
    CfsScreenModeMenu,
    CfsScreenModePicker,
    CfsScreenModeCard,
    CfsScreenModeStatus,
} CfsScreenMode;

typedef struct {
    CfsScreenMode mode;
    CfsMenuModel menu;
    CfsMenuItem menu_items[CFS_SCREEN_MENU_MAX];
    CfsPickerModel picker;
    size_t picker_page;
    size_t basic_count;
    uint8_t hex_r, hex_g, hex_b;
    CfsCardModel card;
    CfsStatusModel status;
    uint8_t ticks; // animation ticks in the current mode
    int8_t spin_left; // menu / picker: steps of the turn after a move (sign = direction)
} CfsScreenModel;

struct CfsScreen {
    View* view;
    FuriTimer* anim_timer;
    FuriTimer* timeout_timer;
    CfsScreenCallback callback;
    void* context;
    uint32_t menu_events[CFS_SCREEN_MENU_MAX];
    uint32_t ev_pick, ev_hex;
    uint32_t ev_left, ev_center, ev_right;
    uint32_t ev_ok, ev_timeout;
};

static void cfs_screen_send(CfsScreen* screen, uint32_t event) {
    if(screen->callback) screen->callback(screen->context, event);
}

// ---- picker pages ----

static const CfsColorPreset cfs_picker_none = {"", 0, 0, 0};

const CfsColorPreset* cfs_picker_color_at(size_t page, size_t selected, size_t basic_count) {
    if(page < CFS_SHADE_FAMILY_COUNT) {
        const CfsShadeFamily* fam = &cfs_shade_families[page];
        return &fam->shades[selected < fam->count ? selected : fam->count - 1];
    }
    if(page == CFS_PICKER_PAGE_BASIC && basic_count) {
        return &cfs_color_table[selected < basic_count ? selected : basic_count - 1];
    }
    return &cfs_picker_none;
}

// Fill the draw model for the current page; keeps the cursor within the list.
static void cfs_picker_load(CfsScreenModel* m) {
    CfsPickerModel* p = &m->picker;
    if(m->picker_page == CFS_PICKER_PAGE_HEX) {
        p->kind = CfsPickerHex;
        p->family = "Custom hex";
        p->shades = NULL;
        p->count = 0;
        p->r = m->hex_r;
        p->g = m->hex_g;
        p->b = m->hex_b;
        return;
    }
    p->kind = CfsPickerList;
    if(m->picker_page == CFS_PICKER_PAGE_BASIC) {
        p->family = "Basic colors";
        p->shades = cfs_color_table;
        p->count = m->basic_count;
    } else {
        const CfsShadeFamily* fam = &cfs_shade_families[m->picker_page];
        p->family = fam->name;
        p->shades = fam->shades;
        p->count = fam->count;
    }
    if(p->count == 0) p->count = 1; // never empty; basic_count is at least 8
    if(p->selected >= p->count) p->selected = p->count - 1;
    p->r = p->shades[p->selected].r;
    p->g = p->shades[p->selected].g;
    p->b = p->shades[p->selected].b;
}

// ---- view callbacks ----

static void cfs_screen_draw(Canvas* canvas, void* model) {
    CfsScreenModel* m = model;
    canvas_clear(canvas);
    switch(m->mode) {
    case CfsScreenModeMenu:
        cfs_draw_menu(canvas, &m->menu);
        break;
    case CfsScreenModePicker:
        cfs_draw_color_picker(canvas, &m->picker);
        break;
    case CfsScreenModeCard:
        cfs_draw_spool_card(canvas, &m->card);
        break;
    case CfsScreenModeStatus:
        cfs_draw_status(canvas, &m->status);
        break;
    default:
        break;
    }
}

static bool
    cfs_screen_input_menu(CfsScreen* screen, CfsScreenModel* m, InputEvent* e, uint32_t* send) {
    CfsMenuModel* menu = &m->menu;
    if(menu->count == 0) return false;
    if((e->type == InputTypeShort || e->type == InputTypeRepeat) &&
       (e->key == InputKeyUp || e->key == InputKeyDown)) {
        // The logo spool works like a dial: each move turns it a bit.
        if(e->key == InputKeyUp) {
            menu->selected = menu->selected ? menu->selected - 1 : menu->count - 1;
            m->spin_left = -2;
        } else {
            menu->selected = (menu->selected + 1) % menu->count;
            m->spin_left = 2;
        }
        if(menu->selected < menu->window) menu->window = menu->selected;
        if(menu->selected >= menu->window + CFS_MENU_ROWS)
            menu->window = menu->selected + 1 - CFS_MENU_ROWS;
        return true;
    }
    if(e->type == InputTypeShort && e->key == InputKeyOk) {
        *send = screen->menu_events[menu->selected];
        return true;
    }
    return false;
}

static bool
    cfs_screen_input_picker(CfsScreen* screen, CfsScreenModel* m, InputEvent* e, uint32_t* send) {
    CfsPickerModel* p = &m->picker;
    const bool press = e->type == InputTypeShort || e->type == InputTypeRepeat;
    if(press && (e->key == InputKeyLeft || e->key == InputKeyRight)) {
        // Families are sorted light -> dark, so the cursor keeps its place
        // (about the same lightness) when switching family.
        if(e->key == InputKeyLeft) {
            m->picker_page = m->picker_page ? m->picker_page - 1 : CFS_PICKER_PAGE_COUNT - 1;
        } else {
            m->picker_page = (m->picker_page + 1) % CFS_PICKER_PAGE_COUNT;
        }
        m->spin_left = e->key == InputKeyLeft ? -4 : 4;
        cfs_picker_load(m);
        return true;
    }
    if(press && (e->key == InputKeyUp || e->key == InputKeyDown) && p->kind == CfsPickerList) {
        if(e->key == InputKeyUp) {
            p->selected = p->selected ? p->selected - 1 : p->count - 1;
            m->spin_left = -3;
        } else {
            p->selected = (p->selected + 1) % p->count;
            m->spin_left = 3;
        }
        cfs_picker_load(m);
        return true;
    }
    if(e->type == InputTypeShort && e->key == InputKeyOk) {
        *send = p->kind == CfsPickerHex ? screen->ev_hex : screen->ev_pick;
        return true;
    }
    return false;
}

static bool
    cfs_screen_input_card(CfsScreen* screen, CfsScreenModel* m, InputEvent* e, uint32_t* send) {
    if(e->type != InputTypeShort) return false;
    if(e->key == InputKeyLeft && m->card.button_left) {
        *send = screen->ev_left;
        return true;
    }
    if(e->key == InputKeyOk && m->card.button_center) {
        *send = screen->ev_center;
        return true;
    }
    if(e->key == InputKeyRight && m->card.button_right) {
        *send = screen->ev_right;
        return true;
    }
    return false;
}

static bool cfs_screen_input(InputEvent* event, void* context) {
    CfsScreen* screen = context;
    bool consumed = false;
    uint32_t send = UINT32_MAX;
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            switch(m->mode) {
            case CfsScreenModeMenu:
                consumed = cfs_screen_input_menu(screen, m, event, &send);
                break;
            case CfsScreenModePicker:
                consumed = cfs_screen_input_picker(screen, m, event, &send);
                break;
            case CfsScreenModeCard:
                consumed = cfs_screen_input_card(screen, m, event, &send);
                break;
            case CfsScreenModeStatus:
                if(event->type == InputTypeShort && event->key == InputKeyOk &&
                   m->status.button_center) {
                    send = screen->ev_ok;
                    consumed = true;
                }
                break;
            default:
                break;
            }
        },
        consumed);
    // Send outside the model lock: the scene's handler may show a new screen.
    if(send != UINT32_MAX) cfs_screen_send(screen, send);
    return consumed;
}

// Spool animation: the menu and picker spools make a short turn after each
// move, the working spool turns (and winds) all the time.
static bool cfs_screen_spin(int8_t* left, uint8_t* angle) {
    if(*left > 0) {
        *angle = (*angle + 1) % CFS_SPOOL_STEPS;
        (*left)--;
        return true;
    }
    if(*left < 0) {
        *angle = (*angle + CFS_SPOOL_STEPS - 1) % CFS_SPOOL_STEPS;
        (*left)++;
        return true;
    }
    return false;
}

static void cfs_screen_anim_tick(void* context) {
    CfsScreen* screen = context;
    bool redraw = false;
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            m->ticks++;
            switch(m->mode) {
            case CfsScreenModeMenu:
                redraw = cfs_screen_spin(&m->spin_left, &m->menu.angle);
                break;
            case CfsScreenModePicker:
                redraw = cfs_screen_spin(&m->spin_left, &m->picker.angle);
                break;
            case CfsScreenModeStatus:
                if(m->status.kind == CfsStatusWorking) {
                    m->status.frame++;
                    redraw = true;
                }
                break;
            default:
                break;
            }
        },
        redraw);
}

static void cfs_screen_start_anim(CfsScreen* screen) {
    furi_timer_start(screen->anim_timer, furi_ms_to_ticks(CFS_SCREEN_ANIM_MS));
}

static void cfs_screen_timeout(void* context) {
    CfsScreen* screen = context;
    cfs_screen_send(screen, screen->ev_timeout);
}

// ---- public API ----

CfsScreen* cfs_screen_alloc(void) {
    CfsScreen* screen = malloc(sizeof(CfsScreen));
    memset(screen, 0, sizeof(CfsScreen));
    screen->view = view_alloc();
    view_allocate_model(screen->view, ViewModelTypeLocking, sizeof(CfsScreenModel));
    view_set_context(screen->view, screen);
    view_set_draw_callback(screen->view, cfs_screen_draw);
    view_set_input_callback(screen->view, cfs_screen_input);
    screen->anim_timer = furi_timer_alloc(cfs_screen_anim_tick, FuriTimerTypePeriodic, screen);
    screen->timeout_timer = furi_timer_alloc(cfs_screen_timeout, FuriTimerTypeOnce, screen);
    return screen;
}

void cfs_screen_free(CfsScreen* screen) {
    furi_check(screen);
    furi_timer_stop(screen->anim_timer);
    furi_timer_stop(screen->timeout_timer);
    furi_timer_free(screen->anim_timer);
    furi_timer_free(screen->timeout_timer);
    view_free(screen->view);
    free(screen);
}

View* cfs_screen_get_view(CfsScreen* screen) {
    return screen->view;
}

void cfs_screen_set_callback(CfsScreen* screen, CfsScreenCallback callback, void* context) {
    screen->callback = callback;
    screen->context = context;
}

static void cfs_screen_stop_timers(CfsScreen* screen) {
    furi_timer_stop(screen->anim_timer);
    furi_timer_stop(screen->timeout_timer);
}

void cfs_screen_reset(CfsScreen* screen) {
    cfs_screen_stop_timers(screen);
    with_view_model(screen->view, CfsScreenModel * m, { m->mode = CfsScreenModeNone; }, false);
}

void cfs_screen_show_menu(
    CfsScreen* screen,
    const CfsMenuItem* items,
    const uint32_t* item_events,
    size_t count,
    size_t selected) {
    cfs_screen_stop_timers(screen);
    if(count > CFS_SCREEN_MENU_MAX) count = CFS_SCREEN_MENU_MAX;
    memcpy(screen->menu_events, item_events, count * sizeof(uint32_t));
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            m->mode = CfsScreenModeMenu;
            memcpy(m->menu_items, items, count * sizeof(CfsMenuItem));
            m->menu.items = m->menu_items;
            m->menu.count = count;
            m->menu.selected = selected < count ? selected : 0;
            m->menu.window =
                m->menu.selected >= CFS_MENU_ROWS ? m->menu.selected + 1 - CFS_MENU_ROWS : 0;
            m->ticks = 0;
            m->spin_left = 0;
        },
        true);
    cfs_screen_start_anim(screen);
}

size_t cfs_screen_get_menu_selected(CfsScreen* screen) {
    size_t sel = 0;
    with_view_model(screen->view, CfsScreenModel * m, { sel = m->menu.selected; }, false);
    return sel;
}

void cfs_screen_show_picker(
    CfsScreen* screen,
    size_t page,
    size_t selected,
    size_t basic_count,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint32_t pick_event,
    uint32_t hex_event) {
    cfs_screen_stop_timers(screen);
    screen->ev_pick = pick_event;
    screen->ev_hex = hex_event;
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            m->mode = CfsScreenModePicker;
            m->picker_page = page < CFS_PICKER_PAGE_COUNT ? page : 0;
            m->basic_count = basic_count;
            m->hex_r = r;
            m->hex_g = g;
            m->hex_b = b;
            m->picker.selected = selected;
            m->spin_left = 0;
            m->ticks = 0;
            cfs_picker_load(m);
        },
        true);
    cfs_screen_start_anim(screen);
}

void cfs_screen_get_picker(CfsScreen* screen, size_t* page, size_t* selected) {
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            *page = m->picker_page;
            *selected = m->picker.selected;
        },
        false);
}

void cfs_screen_show_card(
    CfsScreen* screen,
    const CfsCardModel* card,
    uint32_t left_event,
    uint32_t center_event,
    uint32_t right_event) {
    cfs_screen_stop_timers(screen);
    screen->ev_left = left_event;
    screen->ev_center = center_event;
    screen->ev_right = right_event;
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            m->mode = CfsScreenModeCard;
            m->card = *card;
        },
        true);
}

void cfs_screen_show_status(
    CfsScreen* screen,
    const CfsStatusModel* status,
    uint32_t ok_event,
    uint32_t timeout_ms,
    uint32_t timeout_event) {
    cfs_screen_stop_timers(screen);
    screen->ev_ok = ok_event;
    screen->ev_timeout = timeout_event;
    with_view_model(
        screen->view,
        CfsScreenModel * m,
        {
            m->mode = CfsScreenModeStatus;
            m->status = *status;
            m->status.frame = 0;
        },
        true);
    if(status->kind == CfsStatusWorking) cfs_screen_start_anim(screen);
    if(timeout_ms) furi_timer_start(screen->timeout_timer, furi_ms_to_ticks(timeout_ms));
}
