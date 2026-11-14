#include "src/core/cfs_app_i.h"
#include "src/data/cfs_shades.h"

// Color step: one screen. Left/Right switch family (shade families, then the
// basic palette, then custom hex), Up/Down move through the family's shades,
// OK picks. Scene state keeps the last page and shade as (page << 8) | shade.

static size_t cfs_color_basic_count(const CfsApp* app) {
    return cfs_palette_count(app->settings.color_palette);
}

// Land on the pending color when it is one of the listed colors (stepping back
// from the weight screen, or editing a tag), else on the last place used.
static bool cfs_color_find(const CfsApp* app, size_t* page, size_t* shade) {
    const CfsTagData* d = &app->pending;
    const size_t basic = cfs_color_basic_count(app);
    for(size_t p = 0; p < CFS_PICKER_PAGE_HEX; p++) {
        const size_t count = p == CFS_PICKER_PAGE_BASIC ? basic : cfs_shade_families[p].count;
        for(size_t i = 0; i < count; i++) {
            const CfsColorPreset* c = cfs_picker_color_at(p, i, basic);
            if(c->r == d->color_r && c->g == d->color_g && c->b == d->color_b) {
                *page = p;
                *shade = i;
                return true;
            }
        }
    }
    return false;
}

void cfs_scene_write_color_on_enter(void* context) {
    CfsApp* app = context;
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, CfsSceneWriteColor);
    size_t page = state >> 8;
    size_t shade = state & 0xFF;
    // The hex page is only reopened directly (back from hex input); otherwise
    // prefer the page holding the pending color.
    if(page != CFS_PICKER_PAGE_HEX) cfs_color_find(app, &page, &shade);
    cfs_screen_show_picker(
        app->screen,
        page,
        shade,
        cfs_color_basic_count(app),
        app->pending.color_r,
        app->pending.color_g,
        app->pending.color_b,
        CfsCustomEventPickerPick,
        CfsCustomEventPickerHex);
    view_dispatcher_switch_to_view(app->view_dispatcher, CfsViewScreen);
}

bool cfs_scene_write_color_on_event(void* context, SceneManagerEvent event) {
    CfsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != CfsCustomEventPickerPick && event.event != CfsCustomEventPickerHex) {
        return false;
    }

    size_t page, shade;
    cfs_screen_get_picker(app->screen, &page, &shade);
    scene_manager_set_scene_state(
        app->scene_manager, CfsSceneWriteColor, (uint32_t)((page << 8) | (shade & 0xFF)));

    if(event.event == CfsCustomEventPickerHex) {
        // Seed byte-input with the current pending color, so a picked shade
        // can be fine-tuned by hand.
        app->color_bytes[0] = app->pending.color_r;
        app->color_bytes[1] = app->pending.color_g;
        app->color_bytes[2] = app->pending.color_b;
        scene_manager_next_scene(app->scene_manager, CfsSceneWriteColorCustom);
    } else {
        const CfsColorPreset* c = cfs_picker_color_at(page, shade, cfs_color_basic_count(app));
        app->pending.color_r = c->r;
        app->pending.color_g = c->g;
        app->pending.color_b = c->b;
        scene_manager_next_scene(app->scene_manager, CfsSceneWriteWeight);
    }
    return true;
}

void cfs_scene_write_color_on_exit(void* context) {
    CfsApp* app = context;
    cfs_screen_reset(app->screen);
}
