#pragma once

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>

#include "../compare_checks/nfc_comparator_compare_checks.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Possible states for the NFC Comparator Reader Worker */
typedef enum {
   NfcComparatorReaderWorkerState_Scanning,
   NfcComparatorReaderWorkerState_Polling,
   NfcComparatorReaderWorkerState_Comparing,
   NfcComparatorReaderWorkerState_Stopped
} NfcComparatorReaderWorkerState;

/** Holds all state for the NFC Comparator Reader Worker */
typedef struct {
   Nfc* nfc;
   FuriThread* thread;
   NfcProtocol* protocol;
   NfcComparatorReaderWorkerState state;
   NfcDevice* loaded_nfc_card;
   NfcDevice* scanned_nfc_card;
   NfcPoller* nfc_poller;
   NfcComparatorCompareChecks* compare_checks;
} NfcComparatorReaderWorker;

/** Allocates and initializes a new NFC Comparator Reader Worker */
NfcComparatorReaderWorker*
   nfc_comparator_reader_worker_alloc(NfcComparatorCompareChecks* compare_checks);

/** Frees the resources used by the worker */
void nfc_comparator_reader_worker_free(NfcComparatorReaderWorker* worker);

/** Stops the worker thread */
void nfc_comparator_reader_worker_stop(NfcComparatorReaderWorker* worker);

/** Starts the worker thread */
void nfc_comparator_reader_worker_start(NfcComparatorReaderWorker* worker);

/** Loads an NFC card from the given path */
bool nfc_comparator_reader_worker_set_loaded_nfc_card(
   NfcComparatorReaderWorker* worker,
   const char* path_to_nfc_card);

/** Gets the current state of the worker */
NfcComparatorReaderWorkerState*
   nfc_comparator_reader_worker_get_state(NfcComparatorReaderWorker* worker);

#ifdef __cplusplus
}
#endif
