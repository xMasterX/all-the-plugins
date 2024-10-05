#include "eink_waveshare_i.h"
#include "eink_waveshare_config.h"

#define TAG "WSH_Poller"

typedef NfcCommand (
    *EinkWavesharePollerStateHandler)(Iso14443_3aPoller* poller, NfcEinkScreen* screen);

static bool eink_waveshare_send_data(
    Iso14443_3aPoller* poller,
    const NfcEinkScreen* screen,
    const uint8_t* data,
    const size_t data_len) {
    furi_assert(poller);
    furi_assert(screen);
    furi_assert(data);
    furi_assert(data_len > 0);

    bit_buffer_reset(screen->tx_buf);
    bit_buffer_reset(screen->rx_buf);

    bit_buffer_append_bytes(screen->tx_buf, data, data_len);

    Iso14443_3aError error =
        iso14443_3a_poller_send_standard_frame(poller, screen->tx_buf, screen->rx_buf, 0);

    bool result = false;
    if(error == Iso14443_3aErrorNone) {
        size_t response_len = bit_buffer_get_size_bytes(screen->rx_buf);
        const uint8_t* response = bit_buffer_get_data(screen->rx_buf);

        result = ((response_len == 2) && (response[0] == 0) && (response[1] == 0)) ||
                 ((response_len == 2) && (response[0] == 0xFF) && (response[1] == 0));

    } else {
        FURI_LOG_E(TAG, "Iso14443_3aError: %02X", error);
    }
    return result;
}

static bool eink_waveshare_send_command(
    Iso14443_3aPoller* poller,
    NfcEinkScreen* screen,
    const uint8_t command,
    const uint8_t* command_data,
    const size_t command_data_length) {
    size_t data_len = command_data_length + 2;
    uint8_t* data = malloc(data_len);

    data[0] = EINK_WAVESHARE_COMMAND_SPECIFIC;
    data[1] = command;

    if(command_data_length > 0) {
        furi_assert(command_data);
        memcpy(&data[2], command_data, command_data_length);
    }

    bool result = eink_waveshare_send_data(poller, screen, data, data_len);
    free(data);

    return result;
}

static EinkWavesharePollerState eink_waveshare_poller_state_generic_handler(
    Iso14443_3aPoller* poller,
    NfcEinkScreen* screen,
    uint8_t command,
    EinkWavesharePollerState success_state) {
    bool result = eink_waveshare_send_command(poller, screen, command, NULL, 0);
    return result ? success_state : EinkWavesharePollerStateError;
}

static NfcCommand
    eink_waveshare_poller_state_init(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Init");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_INIT, EinkWavesharePollerStateSelectType);
    if(ctx->poller_state != EinkWavesharePollerStateError) {
        eink_waveshare_on_target_detected(screen);
    }

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_select_type(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Select type");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;
    ctx->poller_state = EinkWavesharePollerStateError;

    do {
        EinkScreenTypeWaveshare screen_type =
            eink_waveshare_config_translate_screen_type_to_protocol(
                screen->data->base.screen_type);
        bool result = eink_waveshare_send_command(
            poller, screen, EINK_WAVESHARE_COMMAND_SELECT_TYPE, &screen_type, sizeof(screen_type));
        if(!result) break;

        ctx->poller_state = EinkWavesharePollerStateSetNormalMode;
    } while(false);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_set_normal_mode(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Mode set");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_NORMAL_MODE, EinkWavesharePollerStateSetConfig1);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_set_config_1(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Set config 1");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_SET_CONFIG_1, EinkWavesharePollerStatePowerOn);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_power_on(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Power On");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_POWER_ON, EinkWavesharePollerStateSetConfig2);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_set_config_2(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Set config 2");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_SET_CONFIG_2, EinkWavesharePollerStateLoadToMain);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_load_to_main(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Load 2 main");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_LOAD_TO_MAIN, EinkWavesharePollerStatePrepareData);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_prepare_data(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Prepare data");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_PREPARE_DATA, EinkWavesharePollerStateSendImageData);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_send_image_data(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Send data");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    uint8_t block_size = screen->data->base.data_block_size;
    size_t data_len = block_size + 3;
    uint8_t* data = malloc(data_len);

    data[0] = EINK_WAVESHARE_COMMAND_SPECIFIC;
    data[1] = EINK_WAVESHARE_COMMAND_DATA_WRITE; //or  EINK_WAVESHARE_COMMAND_DATA_WRITE_V2
    data[2] = block_size;
    memcpy(&data[3], &screen->data->image_data[ctx->data_index], block_size);
    ctx->poller_state = EinkWavesharePollerStateError;

    do {
        bool result = eink_waveshare_send_data(poller, screen, data, data_len);
        if(!result) break;

        ctx->data_index += block_size;
        ctx->block_number++;
        ctx->poller_state = (ctx->block_number == screen->device->block_total) ?
                                EinkWavesharePollerStatePowerOnV2 :
                                EinkWavesharePollerStateSendImageData;

    } while(false);

    eink_waveshare_on_block_processed(screen);
    screen->device->block_current = ctx->block_number;

    free(data);
    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_power_on_v2(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Power On v2");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_POWER_ON_V2, EinkWavesharePollerStateRefresh);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_refresh(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Refresh");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_REFRESH, EinkWavesharePollerStateWaitReady);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_wait_ready(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Wait ready");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_WAIT_FOR_READY, EinkWavesharePollerStateFinish);

    return NfcCommandContinue;
}

static NfcCommand
    eink_waveshare_poller_state_finish(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_D(TAG, "Finish");
    NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;
    NfcCommand command = NfcCommandContinue;

    ctx->poller_state = eink_waveshare_poller_state_generic_handler(
        poller, screen, EINK_WAVESHARE_COMMAND_POWER_OFF, EinkWavesharePollerStateFinish);
    if(ctx->poller_state == EinkWavesharePollerStateFinish) {
        eink_waveshare_on_done(screen);
        command = NfcCommandStop;
    } else {
        ctx->poller_state = EinkWavesharePollerStateError;
    }

    return command;
}

static NfcCommand
    eink_waveshare_poller_state_error_handler(Iso14443_3aPoller* poller, NfcEinkScreen* screen) {
    FURI_LOG_E(TAG, "Error!");
    UNUSED(poller);
    eink_waveshare_on_error(screen);
    return NfcCommandStop;
}

static const EinkWavesharePollerStateHandler handlers[EinkWavesharePollerStateNum] = {
    [EinkWavesharePollerStateInit] = eink_waveshare_poller_state_init,
    [EinkWavesharePollerStateSelectType] = eink_waveshare_poller_state_select_type,
    [EinkWavesharePollerStateSetNormalMode] = eink_waveshare_poller_state_set_normal_mode,
    [EinkWavesharePollerStateSetConfig1] = eink_waveshare_poller_state_set_config_1,
    [EinkWavesharePollerStatePowerOn] = eink_waveshare_poller_state_power_on,
    [EinkWavesharePollerStateSetConfig2] = eink_waveshare_poller_state_set_config_2,
    [EinkWavesharePollerStateLoadToMain] = eink_waveshare_poller_state_load_to_main,
    [EinkWavesharePollerStatePrepareData] = eink_waveshare_poller_state_prepare_data,
    [EinkWavesharePollerStateSendImageData] = eink_waveshare_poller_state_send_image_data,
    [EinkWavesharePollerStatePowerOnV2] = eink_waveshare_poller_state_power_on_v2,
    [EinkWavesharePollerStateRefresh] = eink_waveshare_poller_state_refresh,
    [EinkWavesharePollerStateWaitReady] = eink_waveshare_poller_state_wait_ready,
    [EinkWavesharePollerStateFinish] = eink_waveshare_poller_state_finish,
    [EinkWavesharePollerStateError] = eink_waveshare_poller_state_error_handler,
};

NfcCommand eink_waveshare_poller_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    NfcEinkScreen* screen = context;

    NfcCommand command = NfcCommandContinue;

    Iso14443_3aPollerEvent* Iso14443_3a_event = event.event_data;
    Iso14443_3aPoller* poller = event.instance;

    if(Iso14443_3a_event->type == Iso14443_3aPollerEventTypeReady) {
        NfcEinkWaveshareSpecificContext* ctx = screen->device->screen_context;
        command = handlers[ctx->poller_state](poller, screen);
    }
    if(command == NfcCommandReset) iso14443_3a_poller_halt(poller);
    return command;
}
