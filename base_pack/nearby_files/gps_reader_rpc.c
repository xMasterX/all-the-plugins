#include "gps_reader.h"

#define RPC_STREAM_FREQ       5
#define RPC_POLL_PERIOD_MS    1000
#define RPC_STREAM_TIMEOUT_MS 3000

static void gps_reader_rpc_callback(GpsStatus status, const GpsLocation* location, void* context) {
    GpsReader* gps_reader = context;

    const bool got_fix = (status == GpsStatusOk) && location;

    furi_mutex_acquire(gps_reader->mutex, FuriWaitForever);
    gps_reader->rpc_last_rx = furi_get_tick();
    gps_reader->coordinates.module_detected = true;

    if(got_fix) {
        gps_reader->coordinates.valid = true;
        gps_reader->coordinates.latitude = location->latitude * 1e-7f;
        gps_reader->coordinates.longitude = location->longitude * 1e-7f;
        gps_reader->coordinates.satellite_count = (int)location->satellites;
    } else {
        gps_reader->coordinates.valid = false;
        gps_reader->coordinates.satellite_count = 0;
    }
    furi_mutex_release(gps_reader->mutex);
    gps_reader_blink(gps_reader, got_fix);
}

static void gps_reader_rpc_poll(void* context) {
    GpsReader* gps_reader = context;
    if(furi_get_tick() - gps_reader->rpc_last_rx >= furi_ms_to_ticks(RPC_STREAM_TIMEOUT_MS)) {
        gps_request_stream(gps_reader->rpc, RPC_STREAM_FREQ);
    }
}

void gps_reader_start_rpc(GpsReader* gps_reader) {
    gps_reader->rpc_last_rx = 0;
    gps_reader->rpc = furi_record_open(RECORD_GPS);
    gps_set_location_callback(gps_reader->rpc, gps_reader_rpc_callback, gps_reader);

    gps_request_stream(gps_reader->rpc, RPC_STREAM_FREQ);
    gps_reader->rpc_timer =
        furi_timer_alloc(gps_reader_rpc_poll, FuriTimerTypePeriodic, gps_reader);
    furi_timer_start(gps_reader->rpc_timer, furi_ms_to_ticks(RPC_POLL_PERIOD_MS));
}

void gps_reader_stop_rpc(GpsReader* gps_reader) {
    furi_timer_stop(gps_reader->rpc_timer);
    furi_timer_free(gps_reader->rpc_timer);
    gps_reader->rpc_timer = NULL;

    gps_stop_stream(gps_reader->rpc);
    gps_set_location_callback(gps_reader->rpc, NULL, NULL);
    furi_record_close(RECORD_GPS);
    gps_reader->rpc = NULL;
}
