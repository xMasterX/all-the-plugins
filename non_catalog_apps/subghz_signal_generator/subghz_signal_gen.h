#pragma once

#include <gui/gui.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include <stdint.h>

typedef struct
{
    Gui *gui;
    ViewPort *view_port;
    FuriMessageQueue *event_queue;
    uint32_t frequency;
    uint8_t cursor_position;
    bool running;
    const SubGhzDevice *device;
    SubGhzTxRxWorker *subghz_txrx;
    FuriThread *tx_thread;
    bool tx_running;
    int signal_mode;
} SubGhzSignalGenApp;

typedef enum
{
    SignalModeOok650Async,
    SignalMode2FSKDev238Async,
    SignalMode2FSKDev476Async,
    SignalModeMSK99_97KbAsync,
    SignalModeGFSK9_99KbAsync,
    SignalModePattern0xFF,
    SignalModeSineWave,
    SignalModeSquareWave,
    SignalModeSawtoothWave,
    SignalModeWhiteNoise,
    SignalModeTriangleWave,
    SignalModeChirp,
    SignalModeGaussianNoise,
    SignalModeBurst,
} SignalMode;

SubGhzSignalGenApp *subghz_signal_gen_app_alloc(void);
void subghz_signal_gen_app_free(SubGhzSignalGenApp *app);
int32_t subghz_signal_gen_app(void *p);