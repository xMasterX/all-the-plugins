#include "nrf24tool.h"
#include "libnrf24/nrf24.h"
#include "tx/tx.h"
#include "sniff/sniff.h"
#include "badmouse/badmouse.h"
#include "rx/rx.h"

void app_menu_enter_callback(void* context, uint32_t index) {
    Nrf24Tool* app = (Nrf24Tool*)context;

    switch(index) {
    case MENU_RX:
        view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_RX_SETTINGS);
        break;

    case MENU_TX:
        view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_TX_SETTINGS);
        break;

    case MENU_SNIFFER:
        view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_SNIFF_SETTINGS);
        break;

    case MENU_BADMOUSE:
        bm_read_address(app);
        bm_read_layouts(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_BM_SETTINGS);
        break;

    default:
        return;
    }
}

uint32_t app_menu_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

void no_nrf24_result_callback(DialogExResult result, void* context) {
    Nrf24Tool* app = (Nrf24Tool*)context;
    if(result == DialogExResultLeft) {
        view_dispatcher_switch_to_view(app->view_dispatcher, app->previous_view);
    } else if(result == DialogExResultRight) {
        if(nrf24_check_connected()) {
            view_dispatcher_switch_to_view(app->view_dispatcher, app->next_view);
        }
    }
}

void rx_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    UNUSED(thread);
    Nrf24Tool* app = (Nrf24Tool*)context;

    with_view_model(app->rx_run, RxStatus * status, status->thread_state = state;, true);
}

void tx_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    UNUSED(thread);
    Nrf24Tool* app = (Nrf24Tool*)context;

    with_view_model(app->tx_run, TxStatus * status, status->thread_state = state;, true);
}

void sniff_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    UNUSED(thread);
    Nrf24Tool* app = (Nrf24Tool*)context;

    with_view_model(app->sniff_run, SniffStatus * status, status->thread_state = state;, true);
}

void bm_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    UNUSED(thread);
    Nrf24Tool* app = (Nrf24Tool*)context;

    with_view_model(app->badmouse_run, BadMouseStatus * status, status->thread_state = state;
                    , true);
}
