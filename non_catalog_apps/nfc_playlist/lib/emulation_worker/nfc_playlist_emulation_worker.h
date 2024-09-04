#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>

typedef enum NfcPlaylistEmulationWorkerState {
   NfcPlaylistEmulationWorkerState_Emulating,
   NfcPlaylistEmulationWorkerState_Stopped
} NfcPlaylistEmulationWorkerState;

typedef struct NfcPlaylistEmulationWorker {
   FuriThread* thread;
   NfcPlaylistEmulationWorkerState state;
   NfcListener* nfc_listener;
   NfcDevice* nfc_device;
   NfcProtocol nfc_protocol;
   Nfc* nfc;
} NfcPlaylistEmulationWorker;

NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker_alloc();
void nfc_playlist_emulation_worker_free(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker);
void nfc_playlist_emulation_worker_stop(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker);
void nfc_playlist_emulation_worker_start(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker);

int32_t nfc_playlist_emulation_worker_task(void* context);

bool nfc_playlist_emulation_worker_is_emulating(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker);
void nfc_playlist_emulation_worker_set_nfc_data(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker, char* file_path);
void nfc_playlist_emulation_worker_clear_nfc_data(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker);