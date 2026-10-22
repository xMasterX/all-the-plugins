#pragma once

#include "scheduler_app_i.h"
#include <flipper_format_i.h>

typedef struct ScheduleTxRun ScheduleTxRun;

// line: playlist line to send, or -1 to send the whole file
void scheduler_start_tx(SchedulerApp* context, int16_t line);
// True once the TX thread has stopped, call scheduler_cleanup_tx after
bool scheduler_tx_finished(SchedulerApp* context);
// Free a stopped TX thread
void scheduler_cleanup_tx(SchedulerApp* context);
// Abort a running transmission and wait for the thread to finish
void scheduler_stop_tx(SchedulerApp* context);
