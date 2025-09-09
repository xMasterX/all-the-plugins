#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_power.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <expansion/expansion.h>

#include "menu.h"
#include "uart_utils.h"
#include "settings_storage.h"
#include "settings_def.h"
#include "settings_ui.h"
#include "app_state.h"
#include "callbacks.h"
#include "confirmation_view.h"
#include "utils.h"
#include "app_state.h"

// Include the header where settings_custom_event_callback is declared
#include "settings_ui.h"

#define UART_INIT_STACK_SIZE 2048
#define UART_INIT_TIMEOUT_MS 1500 // ms

int32_t ghost_esp_app(void* p) {
    UNUSED(p);

    // Disable expansion protocol to avoid UART interference
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    // Modified power initialization
    uint8_t attempts = 0;
    bool otg_was_enabled = furi_hal_power_is_otg_enabled();

    // Simply try to enable OTG if not already enabled
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 3) {
        furi_hal_power_enable_otg();
        furi_delay_ms(20);
    }
    furi_delay_ms(50); // Reduced stabilization time

    // Set up bare minimum UI state
    AppState* state = malloc(sizeof(AppState));
    if(!state) return -1;
    memset(state, 0, sizeof(AppState)); // Zero all memory first
    // Open dialogs record for file browser
    state->dialogs = furi_record_open(RECORD_DIALOGS);

    // Initialize menu selection indices
    state->last_wifi_category_index = 0;
    state->last_wifi_scanning_index = 0;
    state->last_wifi_capture_index = 0;
    state->last_wifi_attack_index = 0;
    state->last_wifi_network_index = 0;
    state->last_wifi_settings_index = 0;
    state->last_ble_category_index = 0;
    state->last_ble_scanning_index = 0;
    state->last_ble_capture_index = 0;
    state->last_ble_attack_index = 0;
    state->last_gps_index = 0;
    state->current_index = 0;
    state->current_view = 0;
    state->previous_view = 0;
    state->came_from_settings = false;

    // Initialize essential text buffers with minimal size
    state->textBoxBuffer = malloc(1);
    if(state->textBoxBuffer) {
        state->textBoxBuffer[0] = '\0';
    }
    state->buffer_length = 0;
    state->input_buffer = malloc(INPUT_BUFFER_SIZE);
    if(state->input_buffer) {
        memset(state->input_buffer, 0, INPUT_BUFFER_SIZE);
    }

    // Initialize UI components - core components first
    state->view_dispatcher = view_dispatcher_alloc();
    state->main_menu = main_menu_alloc();
    if(!state->view_dispatcher || !state->main_menu) {
        // Clean up and exit if core components fail
        if(state->view_dispatcher) view_dispatcher_free(state->view_dispatcher);
        if(state->main_menu) main_menu_free(state->main_menu);
        free(state->textBoxBuffer);
        free(state->input_buffer);
        free(state);
        return -1;
    }

    // Allocate remaining UI components
    state->wifi_menu = submenu_alloc();
    state->wifi_scanning_menu = submenu_alloc();
    state->wifi_capture_menu = submenu_alloc();
    state->wifi_attack_menu = submenu_alloc();
    state->wifi_network_menu = submenu_alloc();
    state->wifi_settings_menu = submenu_alloc();
    state->ble_menu = submenu_alloc();
    state->ble_scanning_menu = submenu_alloc();
    state->ble_capture_menu = submenu_alloc();
    state->ble_attack_menu = submenu_alloc();
    state->gps_menu = submenu_alloc();
    state->text_box = text_box_alloc();
    state->settings_menu = variable_item_list_alloc();
    state->text_input = text_input_alloc();
    state->confirmation_view = confirmation_view_alloc();
    state->settings_actions_menu = submenu_alloc();

    // Set headers - only for successfully allocated components
    if(state->main_menu) main_menu_set_header(state->main_menu, "Select a Utility");
    if(state->wifi_menu) submenu_set_header(state->wifi_menu, "Select a Wifi Utility");
    if(state->ble_menu) submenu_set_header(state->ble_menu, "Select a Bluetooth Utility");
    if(state->gps_menu) submenu_set_header(state->gps_menu, "Select a GPS Utility");
    if(state->text_input) text_input_set_header_text(state->text_input, "Enter Your Text");
    if(state->settings_actions_menu) submenu_set_header(state->settings_actions_menu, "Settings");

    // Initialize settings and configuration early
    settings_storage_init();
    if(settings_storage_load(&state->settings, GHOST_ESP_APP_SETTINGS_FILE) != SETTINGS_OK) {
        memset(&state->settings, 0, sizeof(Settings));
        state->settings.stop_on_back_index = 1;
        settings_storage_save(&state->settings, GHOST_ESP_APP_SETTINGS_FILE);
    }

    // Initialize filter config
    state->filter_config = malloc(sizeof(FilterConfig));
    if(state->filter_config) {
        state->filter_config->enabled = state->settings.enable_filtering_index;
        state->filter_config->show_ble_status = true;
        state->filter_config->show_wifi_status = true;
        state->filter_config->show_flipper_devices = true;
        state->filter_config->show_wifi_networks = true;
        state->filter_config->strip_ansi_codes = true;
        state->filter_config->add_prefixes = true;
    }

    // Set up settings UI context
    state->settings_ui_context.settings = &state->settings;
    state->settings_ui_context.send_uart_command = send_uart_command;
    state->settings_ui_context.switch_to_view = NULL;
    state->settings_ui_context.show_confirmation_view = show_confirmation_view_wrapper;
    state->settings_ui_context.context = state;

    // Initialize settings menu
    settings_setup_gui(state->settings_menu, &state->settings_ui_context);

    // Start UART init in background thread
    state->uart_context = uart_init(state);
    if(state->uart_context) {
        FURI_LOG_I("Ghost_ESP", "UART initialized successfully");
    } else {
        FURI_LOG_E("Ghost_ESP", "UART initialization failed");
    }

    // Add views to dispatcher - check each component before adding
    if(state->view_dispatcher) {
        if(state->main_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 0, main_menu_get_view(state->main_menu));
        if(state->wifi_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 1, submenu_get_view(state->wifi_menu));
        if(state->ble_menu)
            view_dispatcher_add_view(state->view_dispatcher, 2, submenu_get_view(state->ble_menu));
        if(state->gps_menu)
            view_dispatcher_add_view(state->view_dispatcher, 3, submenu_get_view(state->gps_menu));
        if(state->settings_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 4, variable_item_list_get_view(state->settings_menu));
        if(state->text_box)
            view_dispatcher_add_view(
                state->view_dispatcher, 5, text_box_get_view(state->text_box));
        if(state->text_input)
            view_dispatcher_add_view(
                state->view_dispatcher, 6, text_input_get_view(state->text_input));
        if(state->confirmation_view)
            view_dispatcher_add_view(
                state->view_dispatcher, 7, confirmation_view_get_view(state->confirmation_view));
        if(state->settings_actions_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 8, submenu_get_view(state->settings_actions_menu));
        if(state->wifi_scanning_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 10, submenu_get_view(state->wifi_scanning_menu));
        if(state->wifi_capture_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 11, submenu_get_view(state->wifi_capture_menu));
        if(state->wifi_attack_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 12, submenu_get_view(state->wifi_attack_menu));
        if(state->wifi_network_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 13, submenu_get_view(state->wifi_network_menu));
        if(state->wifi_settings_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 14, submenu_get_view(state->wifi_settings_menu));
        if(state->ble_scanning_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 20, submenu_get_view(state->ble_scanning_menu));
        if(state->ble_capture_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 21, submenu_get_view(state->ble_capture_menu));
        if(state->ble_attack_menu)
            view_dispatcher_add_view(
                state->view_dispatcher, 22, submenu_get_view(state->ble_attack_menu));

        view_dispatcher_set_custom_event_callback(
            state->view_dispatcher, settings_custom_event_callback);
    }

    if(!state->text_box) {
        FURI_LOG_E("Main", "Text box allocation failed!");
        return -1; // Don't try to fuck with broken UI
    }

    // Show main menu immediately
    show_main_menu(state);

    // Set up and run GUI
    Gui* gui = furi_record_open("gui");
    if(gui && state->view_dispatcher) {
        // Reset any pending custom events that might be in the queue
        view_dispatcher_send_custom_event(state->view_dispatcher, 0);
        furi_delay_ms(5); // Short delay to let events clear

        view_dispatcher_attach_to_gui(state->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
        view_dispatcher_set_navigation_event_callback(state->view_dispatcher, back_event_callback);
        view_dispatcher_set_event_callback_context(state->view_dispatcher, state);
        view_dispatcher_run(state->view_dispatcher);
    }

    // ---- Start Cleanup Sequence ----
    FURI_LOG_I("Ghost_ESP", "Starting cleanup sequence...");

    // Send stop commands if enabled
    if(state && state->settings.stop_on_back_index) {
        FURI_LOG_I("Ghost_ESP", "Sending stop commands...");
        const char* stop_commands[] = {"stop\n"};
        for(size_t i = 0; i < COUNT_OF(stop_commands); i++) {
            // Check if UART context is still valid before sending
            if(state->uart_context && state->uart_context->is_serial_active) {
                send_uart_command(stop_commands[i], state);
                furi_delay_ms(50); // Add delay between commands
            } else {
                FURI_LOG_W(
                    "Ghost_ESP", "UART inactive, skipping stop command: %s", stop_commands[i]);
            }
        }
        furi_delay_ms(100); // Extra delay after sending all stop commands
    }

    // Clean up UART context (this will also handle storage cleanup)
    if(state && state->uart_context) {
        FURI_LOG_I("Ghost_ESP", "Freeing UART context...");
        uart_free(state->uart_context);
        state->uart_context = NULL;
        FURI_LOG_I("Ghost_ESP", "UART context freed.");
    }

    // Remove views from dispatcher
    if(state && state->view_dispatcher) {
        FURI_LOG_I("Ghost_ESP", "Removing views from dispatcher...");
        if(state->main_menu) view_dispatcher_remove_view(state->view_dispatcher, 0);
        if(state->wifi_menu) view_dispatcher_remove_view(state->view_dispatcher, 1);
        if(state->ble_menu) view_dispatcher_remove_view(state->view_dispatcher, 2);
        if(state->gps_menu) view_dispatcher_remove_view(state->view_dispatcher, 3);
        if(state->settings_menu) view_dispatcher_remove_view(state->view_dispatcher, 4);
        if(state->text_box) view_dispatcher_remove_view(state->view_dispatcher, 5);
        if(state->text_input) view_dispatcher_remove_view(state->view_dispatcher, 6);
        if(state->confirmation_view) view_dispatcher_remove_view(state->view_dispatcher, 7);
        if(state->settings_actions_menu) view_dispatcher_remove_view(state->view_dispatcher, 8);
        if(state->wifi_scanning_menu) view_dispatcher_remove_view(state->view_dispatcher, 10);
        if(state->wifi_capture_menu) view_dispatcher_remove_view(state->view_dispatcher, 11);
        if(state->wifi_attack_menu) view_dispatcher_remove_view(state->view_dispatcher, 12);
        if(state->wifi_network_menu) view_dispatcher_remove_view(state->view_dispatcher, 13);
        if(state->wifi_settings_menu) view_dispatcher_remove_view(state->view_dispatcher, 14);
        if(state->ble_scanning_menu) view_dispatcher_remove_view(state->view_dispatcher, 20);
        if(state->ble_capture_menu) view_dispatcher_remove_view(state->view_dispatcher, 21);
        if(state->ble_attack_menu) view_dispatcher_remove_view(state->view_dispatcher, 22);
        FURI_LOG_I("Ghost_ESP", "Views removed.");
        view_dispatcher_free(state->view_dispatcher);
        state->view_dispatcher = NULL;
    }

    // Clear callbacks before cleanup
    if(state && state->confirmation_view) {
        confirmation_view_set_ok_callback(state->confirmation_view, NULL, NULL);
        confirmation_view_set_cancel_callback(state->confirmation_view, NULL, NULL);
    }

    // Cleanup UI components
    FURI_LOG_I("Ghost_ESP", "Freeing UI components...");
    if(state && state->confirmation_view) confirmation_view_free(state->confirmation_view);
    if(state && state->text_input) text_input_free(state->text_input);
    if(state && state->text_box) text_box_free(state->text_box);
    if(state && state->settings_actions_menu) submenu_free(state->settings_actions_menu);
    if(state && state->settings_menu) variable_item_list_free(state->settings_menu);
    if(state && state->wifi_menu) submenu_free(state->wifi_menu);
    if(state && state->wifi_scanning_menu) submenu_free(state->wifi_scanning_menu);
    if(state && state->wifi_capture_menu) submenu_free(state->wifi_capture_menu);
    if(state && state->wifi_attack_menu) submenu_free(state->wifi_attack_menu);
    if(state && state->wifi_network_menu) submenu_free(state->wifi_network_menu);
    if(state && state->wifi_settings_menu) submenu_free(state->wifi_settings_menu);
    if(state && state->ble_menu) submenu_free(state->ble_menu);
    if(state && state->ble_scanning_menu) submenu_free(state->ble_scanning_menu);
    if(state && state->ble_capture_menu) submenu_free(state->ble_capture_menu);
    if(state && state->ble_attack_menu) submenu_free(state->ble_attack_menu);
    if(state && state->gps_menu) submenu_free(state->gps_menu);
    if(state && state->main_menu) main_menu_free(state->main_menu);
    FURI_LOG_I("Ghost_ESP", "UI components freed.");
    // Close GUI record after all GUI-related components are freed
    furi_record_close("gui");
    FURI_LOG_I("Ghost_ESP", "GUI record closed.");

    // Cleanup buffers
    FURI_LOG_I("Ghost_ESP", "Freeing buffers...");
    if(state && state->input_buffer) free(state->input_buffer);
    if(state && state->textBoxBuffer) free(state->textBoxBuffer);
    if(state && state->filter_config) free(state->filter_config);
    FURI_LOG_I("Ghost_ESP", "Buffers freed.");

    // Final state cleanup
    FURI_LOG_I("Ghost_ESP", "Freeing app state...");
    if(state) free(state);
    FURI_LOG_I("Ghost_ESP", "App state freed.");

    // Close record handles at the very end after all resources are freed
    FURI_LOG_I("Ghost_ESP", "Restoring expansion state...");
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
    FURI_LOG_I("Ghost_ESP", "Expansion state restored.");

    // Power cleanup
    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    return 0;
}

// 6675636B796F7564656B69
