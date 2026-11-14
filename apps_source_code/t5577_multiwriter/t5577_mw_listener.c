#include "t5577_mw_listener.h"

#include <furi.h>
#include <furi_hal_rfid.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <bit_lib/bit_lib.h>

#define TAG "T5577MwListen"

// Both taken from lib/lfrfid's own reader, which is the same detector into the same decoder:
// pulses this short are noise rather than signal, and the envelope detector needs this long
// after the field comes up before anything it produces means anything. They are copied by value
// because lib/lfrfid does not export them - see LFRFID_WORKER_READ_MIN_TIME_US and
// LFRFID_WORKER_READ_STABILIZE_TIME_MS in lfrfid_worker_modes.c.
#define LISTEN_MIN_PULSE_US 16
#define LISTEN_STABILIZE_MS 450

/** One bit period as the capture hardware reports it.
 *
 * The capture ISR fires twice per period and the two calls do not carry the same quantity:
 * channel 3 reports the high pulse, and channel 4 reports the *whole* period, rise to rise
 * (furi_hal_rfid.c, furi_hal_capture_dma_isr). The decoder wants the two halves, so the low
 * half is `period - pulse` and the pairing has to happen before anything is fed - feeding the
 * period as the low half makes every second symbol about twice its real length, and nothing
 * decodes at all.
 */
typedef struct {
    uint32_t pulse;
    uint32_t period;
} T5577MwEdge;

/** Periods the ring holds, sized from how long the consumer can be away rather than from how
 * many arrive.
 *
 * The thread sleeps only when the ring is empty, for LISTEN_IDLE_MS, so the most that can pile
 * up is one sleep's worth: 6 ms against 977-1953 periods a second is about 12 entries. 64 is a
 * five times margin on that and 512 bytes rather than 2 KB, and an undersized ring is not a
 * silent fault - `dropped` counts it and the run's closing line reports it.
 *
 * A power of two, so the indices wrap with a mask rather than a division.
 */
#define LISTEN_RING_SIZE 64
#define LISTEN_RING_MASK (LISTEN_RING_SIZE - 1)

/** How long the thread sleeps when the ring is empty.
 *
 * It polls rather than being woken, because waking it is the one thing the ISR must not do -
 * see the ring's own note. The ring holds 130 ms, so 5 ms of latency is slack to spare, and
 * 200 wakeups a second is a fraction of what one-per-period would cost.
 */
#define LISTEN_IDLE_MS 5

/** Bytes the EM4100 dictionary entry is expected to need. Checked against the dictionary once,
 * at alloc, so a firmware that changed the protocol fails at app start rather than mid-listen.
 */
#define LISTEN_KEY_BYTES 8

struct T5577MwListener {
    ProtocolDict* dict;
    size_t data_size; // EM4100 key bytes, <= LISTEN_KEY_BYTES
    FuriThread* thread;
    FuriMutex* mutex; // guards `tally` only
    T5577MwTally tally;

    /** Frames to collect before the run may end, and whether it has.
     *
     * `complete` is set by the listener thread on the frame that completes the run, which is
     * also where its own loop stops - so once it is true the tally is final. Volatile for the
     * mirror of `running`'s reason below: the dispatcher polls it from another thread, and a
     * cached read there leaves the screen listening after the counting has stopped.
     */
    uint16_t min_frames;
    volatile bool complete;

    /** When the last frame decoded, or when the run started if none has.
     *
     * Seeded at start, so "quiet since" needs no second field and no case split. Written by the
     * listener thread, read by the dispatcher through the tally; it answers "has this run
     * stopped collecting", which neither a frame count nor `complete` can.
     */
    volatile uint32_t last_frame_tick;

    /** Periods from the capture ISR to the decoding thread.
     *
     * Hand-rolled rather than a FuriStreamBuffer, which must not be fed from an interrupt in
     * this firmware: furi_stream_buffer_send() goes on to call furi_event_loop_link_notify(),
     * which takes a critical section and can furi_crash(), and at roughly fifteen hundred
     * periods a second that wedges the device below the CLI. furi_message_queue_put() reaches the same
     * notify on its own ISR branch, so it is no alternative. lib/lfrfid's own reader has the
     * same problem and solves it with lib/toolbox/buffer_stream.c, which the SDK does not
     * export - hence this.
     *
     * One producer (the ISR) owns `head`, one consumer (the thread) owns `tail`, and neither
     * writes the other's index, so no lock is needed: on this single core, aligned 32 bit
     * loads and stores are atomic, and a barrier publishes the slot before the index that
     * reveals it. Empty is head == tail, so the ring holds SIZE - 1 periods.
     */
    T5577MwEdge* ring;
    volatile uint32_t head;
    volatile uint32_t tail;

    /** Asks the thread to stop.
     *
     * Volatile because it is written by the dispatcher thread and read by the listener's own
     * loop. Without it a build that inlines the loop body is free to hoist the test out, and
     * then furi_thread_join() never returns - which hangs the GUI service, not just this app.
     */
    volatile bool running;

    // Capture-ISR state. Touched only by the ISR, between capture start and capture stop.
    bool ignore_next_pulse;
    bool have_pulse;
    uint32_t pulse;
    uint32_t dropped; // periods the ring had no room for
};

// Runs in the capture interrupt. Pairs the two halves of a period and puts them in the ring -
// no syscall, no critical section, nothing that can block or notify. The decoder is far too
// heavy to run here and the dictionary is not interrupt-safe.
static void t5577_mw_listener_capture(bool level, uint32_t duration, void* context) {
    T5577MwListener* instance = context;

    // Read and cleared once: any way out of here but a clean rising edge leaves no pulse to
    // pair with, and saying so at each exit invites the fifth one to forget.
    const bool have_pulse = instance->have_pulse;
    instance->have_pulse = false;

    // A spike splits one real pulse in two, so the half after it is discarded with it.
    if(instance->ignore_next_pulse) {
        instance->ignore_next_pulse = false;
        return;
    }

    if(duration <= LISTEN_MIN_PULSE_US) {
        if(level) instance->ignore_next_pulse = true;
        return;
    }

    if(level) {
        instance->pulse = duration;
        instance->have_pulse = true;
        return;
    }

    // A period whose rising edge was never seen, or one the counter wrapped through, says
    // nothing about the two halves.
    if(!have_pulse || duration <= instance->pulse) return;

    const uint32_t head = instance->head;
    const uint32_t next = (head + 1) & LISTEN_RING_MASK;

    if(next == instance->tail) {
        // Counted rather than ignored: dropped periods break the decode, and a tally that
        // comes back empty with a high drop count is a different fault from a quiet tag.
        instance->dropped++;
        return;
    }

    instance->ring[head].pulse = instance->pulse;
    instance->ring[head].period = duration;

    // The slot has to be visible before the index that publishes it.
    __DMB();
    instance->head = next;
}

// Count one decoded frame. Called on the listener thread only.
static void t5577_mw_listener_count(T5577MwListener* instance, uint64_t key) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);

    T5577MwTally* tally = &instance->tally;
    tally->reads++;
    instance->last_frame_tick = furi_get_tick();

    bool counted = false;
    for(uint8_t i = 0; i < tally->keys && !counted; i++) {
        if(tally->key[i] != key) continue;
        tally->count[i]++;
        counted = true;
    }

    if(!counted) {
        if(tally->keys < T5577_MW_TALLY_KEYS) {
            tally->key[tally->keys] = key;
            tally->count[tally->keys] = 1;
            tally->keys++;
        } else {
            tally->crowded++;
        }
    }

    // Settled here, a frame at a time, because this is the only place that sees each frame as
    // it arrives - and the frame to stop on is an exact one, not a range. The caller polls far
    // too coarsely to catch it: a three key tag sends about thirty frames a second.
    // keys is at least 1 by here - the frame above was just counted - so the modulo is safe;
    // the test is kept as the local proof of that, not as a case that fires.
    instance->complete = tally->keys > 0 && tally->reads >= t5577_mw_listener_target(
                                                                instance->min_frames, tally->keys);

    furi_mutex_release(instance->mutex);
}

// Feed one period to the decoder and count a frame if it completed one.
static void t5577_mw_listener_feed(T5577MwListener* instance, T5577MwEdge edge) {
    // The two halves, in the order the decoder expects them. By id rather than by feature:
    // this counts the keys this app writes, and a stray decode as some other ASK protocol
    // would be a different question with a different answer. Nothing here waits for frames to
    // agree, which is the whole point.
    ProtocolId decoded =
        protocol_dict_decoders_feed_by_id(instance->dict, LFRFIDProtocolEM4100, true, edge.pulse);

    if(decoded == PROTOCOL_NO) {
        decoded = protocol_dict_decoders_feed_by_id(
            instance->dict, LFRFIDProtocolEM4100, false, edge.period - edge.pulse);
    }

    if(decoded == PROTOCOL_NO) return;

    uint8_t data[LISTEN_KEY_BYTES] = {0};
    protocol_dict_get_data(instance->dict, LFRFIDProtocolEM4100, data, instance->data_size);
    t5577_mw_listener_count(instance, bit_lib_bytes_to_num_be(data, instance->data_size));
}

static int32_t t5577_mw_listener_thread(void* context) {
    T5577MwListener* instance = context;

    furi_hal_rfid_tim_read_start(125000, 0.5);

    // Broken into slices rather than one delay: _stop() joins this thread from the dispatcher,
    // and a single 450 ms sleep would hold the whole GUI for however much of it was left.
    for(uint32_t waited = 0; waited < LISTEN_STABILIZE_MS && instance->running; waited += 25) {
        furi_delay_ms(25);
    }

    protocol_dict_decoders_start(instance->dict);
    furi_hal_rfid_tim_read_capture_start(t5577_mw_listener_capture, instance);

    FURI_LOG_I(TAG, "listening at 125 kHz");

    uint32_t periods = 0;

    while(instance->running && !instance->complete) {
        // This thread is the only writer of `tail`, so one read of it is enough; `head` stays
        // a volatile read because the ISR moves it.
        const uint32_t tail = instance->tail;

        if(tail == instance->head) {
            furi_delay_ms(LISTEN_IDLE_MS);
            continue;
        }

        // The index was published after the slot was written, so the slot is readable.
        __DMB();

        const T5577MwEdge edge = instance->ring[tail];
        instance->tail = (tail + 1) & LISTEN_RING_MASK;
        periods++;

        t5577_mw_listener_feed(instance, edge);
    }

    furi_hal_rfid_tim_read_capture_stop();
    furi_hal_rfid_tim_read_stop();
    furi_hal_rfid_pins_reset();

    // The one line that separates "the tag is quiet", "the decoder never locked" and "the ring
    // overflowed", which look identical on screen.
    FURI_LOG_I(
        TAG,
        "stopped: %lu periods, %u frames, %u keys, %lu dropped",
        periods,
        instance->tally.reads,
        instance->tally.keys,
        instance->dropped);

    return 0;
}

T5577MwListener* t5577_mw_listener_alloc(ProtocolDict* dict) {
    furi_assert(dict);

    T5577MwListener* instance = malloc(sizeof(T5577MwListener));

    memset(instance, 0, sizeof(T5577MwListener));
    instance->dict = dict;
    instance->data_size = protocol_dict_get_data_size(dict, LFRFIDProtocolEM4100);
    furi_check(instance->data_size <= LISTEN_KEY_BYTES);
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    return instance;
}

void t5577_mw_listener_free(T5577MwListener* instance) {
    furi_assert(instance);

    t5577_mw_listener_stop(instance);

    furi_mutex_free(instance->mutex);
    free(instance);
}

void t5577_mw_listener_start(T5577MwListener* instance, uint16_t min_frames) {
    furi_assert(instance);

    if(instance->running) return;

    instance->min_frames = min_frames;
    instance->complete = false;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    memset(&instance->tally, 0, sizeof(instance->tally));
    furi_mutex_release(instance->mutex);

    instance->ignore_next_pulse = false;
    instance->have_pulse = false;
    instance->dropped = 0;
    instance->last_frame_tick = furi_get_tick();
    instance->head = 0;
    instance->tail = 0;

    // Before the thread starts, because the capture ISR the thread installs writes to it. Per
    // run rather than for the life of the app, since only the test screen ever listens.
    instance->ring = malloc(LISTEN_RING_SIZE * sizeof(T5577MwEdge));
    instance->running = true;

    // Above the dispatcher, so a burst of periods is drained while the screen is being drawn
    // rather than after it.
    instance->thread =
        furi_thread_alloc_ex("T5577MwListen", 4096, t5577_mw_listener_thread, instance);
    // 4 KB rather than 2. The loop itself is shallow and FURI_LOG_I formats into a heap
    // FuriString rather than onto this stack, so the real peak is well under 1 KB - but it is
    // transient memory and the failure mode of getting it wrong is a device crash, so the
    // margin stays where measurement, not arithmetic, would be needed to shrink it.
    furi_thread_set_priority(instance->thread, FuriThreadPriorityHigh);
    furi_thread_start(instance->thread);
}

void t5577_mw_listener_stop(T5577MwListener* instance) {
    furi_assert(instance);

    if(!instance->running) return;

    // The thread tears the capture down itself, before it returns, so the ISR has stopped by
    // the time the join does - which is what makes freeing the ring below safe.
    instance->running = false;
    furi_thread_join(instance->thread);
    furi_thread_free(instance->thread);
    instance->thread = NULL;

    free(instance->ring);
    instance->ring = NULL;
}

bool t5577_mw_listener_complete(const T5577MwListener* instance) {
    furi_assert(instance);

    return instance->complete;
}

void t5577_mw_listener_tally(T5577MwListener* instance, T5577MwTally* tally) {
    furi_assert(instance);
    furi_assert(tally);

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    *tally = instance->tally;
    furi_mutex_release(instance->mutex);

    // Copied in rather than kept under the mutex: the ISR cannot take a lock, and an aligned
    // 32 bit read of a word only it writes is as sound here as the ring indices already are.
    tally->dropped = instance->dropped;

    tally->quiet_ticks = furi_get_tick() - instance->last_frame_tick;
}
