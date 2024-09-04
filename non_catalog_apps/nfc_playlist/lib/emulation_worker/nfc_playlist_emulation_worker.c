#include "nfc_playlist_emulation_worker.h"

NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker_alloc() {
   NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker = malloc(sizeof(NfcPlaylistEmulationWorker));
   nfc_playlist_emulation_worker->thread = furi_thread_alloc_ex("NfcPlaylistEmulationWorker", 4096, nfc_playlist_emulation_worker_task, nfc_playlist_emulation_worker);
   nfc_playlist_emulation_worker->state = NfcPlaylistEmulationWorkerState_Stopped;
   nfc_playlist_emulation_worker->nfc = nfc_alloc();
   nfc_playlist_emulation_worker->nfc_device = nfc_device_alloc();
   return nfc_playlist_emulation_worker;
}

void nfc_playlist_emulation_worker_free(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker) {
   furi_assert(nfc_playlist_emulation_worker);
   furi_thread_free(nfc_playlist_emulation_worker->thread);
   nfc_free(nfc_playlist_emulation_worker->nfc);
   nfc_device_free(nfc_playlist_emulation_worker->nfc_device);
   free(nfc_playlist_emulation_worker);
}

void nfc_playlist_emulation_worker_stop(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker) {
   furi_assert(nfc_playlist_emulation_worker);
   if (nfc_playlist_emulation_worker->state != NfcPlaylistEmulationWorkerState_Stopped) {
      nfc_playlist_emulation_worker->state = NfcPlaylistEmulationWorkerState_Stopped;
      furi_thread_join(nfc_playlist_emulation_worker->thread);
   }
}

void nfc_playlist_emulation_worker_start(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker) {
   furi_assert(nfc_playlist_emulation_worker);
   nfc_playlist_emulation_worker->state = NfcPlaylistEmulationWorkerState_Emulating;
   furi_thread_start(nfc_playlist_emulation_worker->thread);
}

int32_t nfc_playlist_emulation_worker_task(void* context) {
   NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker = context;

   if (nfc_playlist_emulation_worker->state == NfcPlaylistEmulationWorkerState_Emulating) {

      nfc_playlist_emulation_worker->nfc_listener =
         nfc_listener_alloc(nfc_playlist_emulation_worker->nfc,
            nfc_playlist_emulation_worker->nfc_protocol,
            nfc_device_get_data(nfc_playlist_emulation_worker->nfc_device, nfc_playlist_emulation_worker->nfc_protocol)
         );
      nfc_listener_start(nfc_playlist_emulation_worker->nfc_listener, NULL, NULL);

      while(nfc_playlist_emulation_worker->state == NfcPlaylistEmulationWorkerState_Emulating) {
         furi_delay_ms(50);
      }

      nfc_listener_stop(nfc_playlist_emulation_worker->nfc_listener);
      nfc_listener_free(nfc_playlist_emulation_worker->nfc_listener);
   }

   nfc_playlist_emulation_worker->state = NfcPlaylistEmulationWorkerState_Stopped;

   return 0;
}

bool nfc_playlist_emulation_worker_is_emulating(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker) {
   furi_assert(nfc_playlist_emulation_worker);
   return nfc_playlist_emulation_worker->state == NfcPlaylistEmulationWorkerState_Emulating;
}

void nfc_playlist_emulation_worker_set_nfc_data(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker, char* file_path) {
   furi_assert(nfc_playlist_emulation_worker);
   nfc_device_load(nfc_playlist_emulation_worker->nfc_device, file_path);
   nfc_playlist_emulation_worker->nfc_protocol = nfc_device_get_protocol(nfc_playlist_emulation_worker->nfc_device);
}

void nfc_playlist_emulation_worker_clear_nfc_data(NfcPlaylistEmulationWorker* nfc_playlist_emulation_worker) {
   furi_assert(nfc_playlist_emulation_worker);
   nfc_device_clear(nfc_playlist_emulation_worker->nfc_device);
}