#include "../lidaremulator_app_i.h"
#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/view.h>
#include <gui/view_port.h>
#include "../view_hijacker.h"

#define ARRAY_SIZE(x)  ((int)(sizeof(x) / sizeof(0[x])))

enum SubmenuIndex {
    SubmenuIndexPredefinedGUNs,
};

typedef uint32_t GunTiming;

typedef struct {
    char name[40];
    GunTiming timing;
} GunDef;

const GunDef guns[] = {
    { "   67.6 - Zurad Rapid Laser", 14800 },
    { "   68.1 - Laser Atlanta Stealth", 4194 },              
    { "   75.0 - Traffic Observer LMS291/221", 13333 },
    { "   99.9 - Vitronic PoliScan Speed", 10020 },
    { "  100.0 - Ultralyte Rev.1", 10000 },
    { "  100.0 - Jenoptik LaserPL", 10000 },
    { "  125.0 - Ultralyte Rev.2", 8000 },
    { "  130.0 - Stalker LZ-1", 7693 },
    { "  190.0 - Dragon Eye", 6667 },
    { "  200.0 - Kustom PL3", 5000 },
    { "  200.0 - Kustom PL4", 5000 },
    { "  200.0 - Kustom PLite", 5000 },
    { "  200.0 - VHT Fama III", 5000 },
    { "  200.3 - LTI Truspeed US", 4993 }, 
    { "  238.4 - Kustom PL2", 4194 },
    { "  238.4 - VHT Fama II", 4194 },
    { "  238.4 - Laser Atlanta", 4194 },
    { "  380.0 - Kustom PL1", 2631 },
    { "  384.0 - TSS Laser 500", 2603 },
    { "  400.0 - Elite 1500", 2500 },
    { "  600.0 - Jenoptik Laveg", 1666 },
    { " 3205.0 - NJL SCS-102", 312 },
    { " 3205.0 - NJL SCS-103", 312 },
    { " 4000.0 - LTI Truspeed S", 250 },
    { " 4673.0 - Riegl FG21-P", 214 },
};

static void lidaremulator_scene_predefined_guns_submenu_callback(void* context, uint32_t index) {
    LidarEmulatorApp* lidaremulator = context;

    view_dispatcher_send_custom_event(lidaremulator->view_dispatcher, index);
}

bool lidaremulator_scene_predefined_guns_view_on_event(InputEvent* event, void* context) {
    bool consumed = false;
    
    ViewHijacker* view_hijacker = context;
    furi_check(context);

    LidarEmulatorApp* lidaremulator = view_hijacker->new_input_callback_context;
    furi_check(lidaremulator);

    Submenu* submenu = lidaremulator->submenu;
    furi_check(submenu);

    if (event->type == InputTypeShort && event->key == InputKeyOk) {
        consumed = true;
    }

    if (event->type == InputTypePress && event->key == InputKeyOk) {
        static uint32_t timing;

        uint32_t idx = submenu_get_selected_item(submenu);
        furi_check( idx < (uint32_t)ARRAY_SIZE(guns) );

        furi_hal_light_set(LightRed,0);
        furi_hal_light_set(LightGreen,0);
        furi_hal_light_set(LightBlue,255);

        const GpioPin* const pin_led = &gpio_infrared_tx;
        const GpioPin* const pin_ok = &gpio_button_ok;

        timing = guns[idx].timing-1;
        furi_hal_gpio_init(pin_led, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
        if (idx==1) {
            // laser atlanta stealth mode

            uint32_t timing2=(timing+1)*6-1;
            do {
                furi_hal_gpio_write(pin_led, true);
                furi_delay_us(1);
                furi_hal_gpio_write(pin_led, false);
                furi_delay_us(timing);
                furi_hal_gpio_write(pin_led, true);
                furi_delay_us(1);
                furi_hal_gpio_write(pin_led, false);
                furi_delay_us(timing2);

                // Do until button is down
            } while(  furi_hal_gpio_read(pin_ok));

        } else {
            // common gun pattern

            do {
                furi_hal_gpio_write(pin_led, true);
                furi_delay_us(1);
                furi_hal_gpio_write(pin_led, false);
                furi_delay_us(timing);

                // Do until button is down
            } while(  furi_hal_gpio_read(pin_ok));
        }

        furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

        consumed = true;
    }

    if (event->type == InputTypeRelease && event->key == InputKeyOk) {
        
        furi_hal_light_set(LightRed,0);
        furi_hal_light_set(LightGreen,0);
        furi_hal_light_set(LightBlue,0);

        consumed = true;
    }

    if ( ( ! consumed ) && (view_hijacker->orig_input_callback) )
            consumed = view_hijacker->orig_input_callback(event, view_hijacker->orig_context);

    return consumed;
}


void lidaremulator_scene_predefined_guns_on_enter(void* context) {
    LidarEmulatorApp* lidaremulator = context;
    furi_check(lidaremulator);

    Submenu* submenu = lidaremulator->submenu;
    furi_check(submenu);

    SceneManager* scene_manager = lidaremulator->scene_manager;
    furi_check(scene_manager);

    for(unsigned i=0; i<ARRAY_SIZE(guns); i++) 
        submenu_add_item(submenu, guns[i].name, i, lidaremulator_scene_predefined_guns_submenu_callback, lidaremulator);

    const uint32_t submenu_index = scene_manager_get_scene_state(scene_manager, LidarEmulatorScenePredefinedGUNs);
    submenu_set_selected_item(submenu, submenu_index);
    scene_manager_set_scene_state(scene_manager, LidarEmulatorScenePredefinedGUNs, 0);

    view_dispatcher_switch_to_view(lidaremulator->view_dispatcher, LidarEmulatorViewSubmenu);

    view_hijacker_attach_to_view_dispacher_current(lidaremulator->view_hijacker, lidaremulator->view_dispatcher);

    view_hijacker_hijack_input_callback(lidaremulator->view_hijacker, lidaremulator_scene_predefined_guns_view_on_event, lidaremulator);

}

bool lidaremulator_scene_predefined_guns_on_event(void* context, SceneManagerEvent event) {
    LidarEmulatorApp* lidaremulator = context;
    furi_check(lidaremulator);

    SceneManager* scene_manager = lidaremulator->scene_manager;
    furi_check(lidaremulator);

    bool consumed = false;

    if (event.type == SceneManagerEventTypeCustom) {
        const uint32_t submenu_index = event.event;
        scene_manager_set_scene_state(scene_manager, LidarEmulatorScenePredefinedGUNs, submenu_index);

        if (submenu_index == 0) {
            furi_hal_light_set(LightRed,255);
            furi_hal_light_set(LightGreen,0);
            furi_hal_light_set(LightBlue,0);
        } 

        consumed = true;
    }

    return consumed;
}

void lidaremulator_scene_predefined_guns_on_exit(void* context) {
    LidarEmulatorApp* lidaremulator = context;
    furi_check(lidaremulator);

    view_hijacker_detach_from_view(lidaremulator->view_hijacker);

    submenu_reset(lidaremulator->submenu);
}
