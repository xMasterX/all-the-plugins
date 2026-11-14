#include "live_ak09911.h"

#include <furi.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// AKM AK09911, a 3-axis electronic compass.
//
// Where the numbers come from, and where they stop. The operation modes, the
// mode-transition rule and the sensitivity are quoted below from the AKM
// AK09911 ShortDatasheet-E-00 (2014/1), which is the document that could be
// obtained. It has no register map: the register addresses and the status-bit
// positions are from issue #38's citation of the full MS1526-E-01 sections
// 6.3-6.4 and 8.3.3-8.3.11, cross-checked against what a purple AK09911C
// breakout actually answered on the bench on 8 Sep 2026. Anything below marked
// "MS1526" is on that footing rather than on a datasheet read here.
//
// NOT RUN ON HARDWARE. Every byte of this file is derived from a bench capture
// somebody else took with a different tool. It compiles and it follows the
// documented sequence; nobody has yet watched it drive a part.

// MS1526: WIA1 is the AKM company code, WIA2 the device code. Both fixed, and
// both are read before anything is written -- 0x0C and 0x0D are shared with the
// QMC5883L, whose whole signature is one register reading 0xFF, so an address
// that acknowledges proves nothing at all here.
#define AK_REG_WIA1   0x00
#define AK_REG_WIA2   0x01
#define AK_WIA1_AKM   0x48
#define AK_WIA2_09911 0x05

// MS1526: ST1 at 0x10, then the three little-endian signed 16-bit axes, a dummy
// byte the datasheet calls TMPS, and ST2 last. All nine are read in one burst
// because ST2 is what releases the held sample -- stopping short of it leaves
// the part sitting on the same measurement forever.
#define AK_REG_ST1   0x10
#define AK_BURST_LEN 9 // 0x10 through 0x18
#define AK_ST1_DRDY  0x01 // a fresh measurement is waiting
#define AK_ST1_DOR   0x02 // a measurement was dropped before this one was read
#define AK_ST2_HOFL  0x08 // the sensor overflowed; the data is not a reading

// CNTL2, and the five mode bits in it. ShortDatasheet-E-00 figure 6.1:
// MODE[4:0] = 00000 power-down, 00010 "Continuous measurement mode 1 -- sensor
// is measured periodically in 10Hz", 10000 "Self-test mode -- sensor is
// self-tested and the result is output. Transits to Power-down mode
// automatically", 11111 "Fuse ROM access mode". MS1526 places the register at
// 0x31 and the fuse-ROM sensitivity bytes at 0x60-0x62.
#define AK_REG_CNTL2      0x31
#define AK_MODE_POWERDOWN 0x00
#define AK_MODE_CONT_10HZ 0x02
#define AK_MODE_SELFTEST  0x10
#define AK_MODE_FUSE_ROM  0x1F
#define AK_REG_ASAX       0x60

// ShortDatasheet-E-00 section 6.3: "When user wants to change operation mode,
// transit to Power-down mode first and then transit to other modes. After
// Power-down mode is set, at least 100 us (Twat) is needed before setting
// another mode." One millisecond is an order of magnitude over that and is the
// smallest delay this app can express, so every mode change pays it.
#define AK_MODE_WAIT_MS 1

// A measurement at 10 Hz is 100 ms away at worst, and a self-test is one
// single-shot conversion. Poll at 20 ms so a fresh sample is never more than
// that stale, and give up after 500 ms -- five times the slowest expected wait,
// and short enough that a part which has stopped converting is noticed rather
// than hung on.
#define AK_POLL_MS           20
#define AK_SAMPLE_TIMEOUT_MS 500

// ShortDatasheet-E-00 electrical characteristics: 0.6 uT/LSB typical, after the
// fuse-ROM sensitivity adjustment. Kept as a float because the heading is the
// only place it is used and raw counts are what the lines show.
#define AK_UT_PER_LSB 0.6f

// MS1526 self-test limits, in ADJUSTED counts -- the sensitivity correction
// has to be applied before comparing, or a part with an unremarkable ASA is
// failed for it. The internal magnetic source drives Z hard negative, which is
// why its window is nowhere near the other two.
#define AK_SELFTEST_XY_LIMIT 30
#define AK_SELFTEST_Z_MIN    (-400)
#define AK_SELFTEST_Z_MAX    (-50)

// ponytail: a heuristic, not a specification, and the first version of it was
// wrong in a way only hardware could show. It was set to 100 from a bench
// capture where waving the module gave 146/812/261 counts -- but 812 counts is
// 487 uT, ten times the earth's field, so something magnetic was sitting next
// to the sensor during that capture. Against the earth alone the whole vector
// is only about 50 raw counts here, and one axis cannot swing further than
// twice that even when turned exactly end over end. A hundred counts on two
// axes was therefore above the physical maximum: the test could not pass, and
// on 9 Sep 2026 somebody turned the board for two and a half minutes proving
// it -- 1545 reads, swings plateaued at 38/47/26 and the counter never left
// 0/2.
//
// Thirty is set from that same session: stationary noise was 4/5/7 counts, so
// it sits four to seven times above the noise, and ordinary handling reached
// 38 and 47 on two axes. The weakest place on earth has a field about half of
// this one's, where 30 still needs most of a full inversion -- if it proves
// unreachable somewhere, re-measure both numbers there rather than trusting
// these. Measure the still board first: the noise floor is the half that says
// whether a threshold means anything.
#define AK_MOVE_COUNTS 30
#define AK_MOVE_AXES   2

// Two things are proven here, and they are proven separately: the part's own
// self-test says the magnetic sensor works, and the field it reports afterwards
// changes when the board is waved. Neither implies the other -- a canned
// self-test answer would not move, and a noisy channel would not pass the
// self-test -- which is why both boxes have to fill.
#define AK_PROOF_STEPS 2

typedef struct {
    int32_t min;
    int32_t max;
} AxisRange;

static void ak_delay(const volatile bool* stop, uint32_t ms) {
    while(ms && !*stop) {
        uint32_t chunk = ms > 40 ? 40 : ms;
        furi_delay_ms(chunk);
        ms -= chunk;
    }
}

static LiveTestIdResult ak_identify(const LiveTestI2c* i2c, uint8_t addr7) {
    uint8_t wia1 = 0, wia2 = 0;
    if(!live_test_read_id8(i2c, addr7, AK_REG_WIA1, &wia1))
        return live_test_id_unreadable(i2c, addr7);
    if(!live_test_read_id8(i2c, addr7, AK_REG_WIA2, &wia2))
        return live_test_id_unreadable(i2c, addr7);
    return (wia1 == AK_WIA1_AKM && wia2 == AK_WIA2_09911) ? LiveTestIdMatch : LiveTestIdMismatch;
}

// Every mode change goes through power-down and waits Twat, because the
// datasheet says every mode change goes through power-down. Returns false if
// either write was not acknowledged, and the caller must then treat the part as
// still configured -- the first write may well have landed.
static bool
    ak_set_mode(const LiveTestI2c* i2c, uint8_t addr7, const volatile bool* stop, uint8_t mode) {
    if(!i2c->write_reg(addr7, AK_REG_CNTL2, AK_MODE_POWERDOWN, LIVE_TEST_TIMEOUT_MS)) return false;
    ak_delay(stop, AK_MODE_WAIT_MS);
    if(mode == AK_MODE_POWERDOWN) return true;
    if(!i2c->write_reg(addr7, AK_REG_CNTL2, mode, LIVE_TEST_TIMEOUT_MS)) return false;
    ak_delay(stop, AK_MODE_WAIT_MS);
    return true;
}

// Three outcomes, because they call for three different responses and folding
// them together is how a strong magnet gets reported as a broken sensor.
typedef enum {
    AkSampleOk,
    AkSampleOverflow, // the sensor saturated: discard it, but nothing is wrong
    AkSampleFailed, // the bus failed, or nothing became ready in time
} AkSampleResult;

// One measurement, start to finish: wait for DRDY, then read all nine bytes so
// that ST2 releases the sample. Stopping short of ST2 leaves the part sitting
// on the same measurement forever, which reads as a working sensor with a
// beautifully stable field.
//
// An overflowed sample is not a small reading, it is not a reading at all, so
// its bytes are never returned -- printing them would be printing a number the
// part did not stand behind.
//
// `dropped` counts DOR separately again. A measurement going unread is not an
// error on the bus and not a fault in the part; at 10 Hz it means this loop was
// busy, and it is worth showing without being counted as a failure.
static AkSampleResult ak_read_sample(
    const LiveTestI2c* i2c,
    uint8_t addr7,
    const volatile bool* stop,
    int32_t out[3],
    uint16_t* dropped) {
    for(uint32_t waited = 0; waited < AK_SAMPLE_TIMEOUT_MS && !*stop; waited += AK_POLL_MS) {
        uint8_t buf[AK_BURST_LEN] = {0};
        if(!i2c->read_mem(addr7, AK_REG_ST1, buf, sizeof(buf), LIVE_TEST_TIMEOUT_MS))
            return AkSampleFailed;

        if(!(buf[0] & AK_ST1_DRDY)) {
            ak_delay(stop, AK_POLL_MS);
            continue;
        }
        if(buf[0] & AK_ST1_DOR) (*dropped)++;
        if(buf[8] & AK_ST2_HOFL) return AkSampleOverflow;

        for(uint8_t axis = 0; axis < 3; axis++) {
            // Little-endian, low byte first. The MPU family is the other way
            // round, and getting this backwards produces numbers that look
            // entirely plausible.
            out[axis] =
                (int16_t)((uint16_t)buf[1 + axis * 2] | ((uint16_t)buf[2 + axis * 2] << 8));
        }
        return AkSampleOk;
    }
    return AkSampleFailed;
}

// Hadj = H * (1 + ASA/128), from MS1526. This is the factory sensitivity
// correction and nothing else: it is not hard- or soft-iron calibration, and a
// part whose fuse ROM could not be read is measured uncorrected rather than
// measured wrong.
static int32_t ak_adjust(int32_t raw, uint8_t asa) {
    if(!asa) return raw;
    return raw * (128 + (int32_t)asa) / 128;
}

static void ak_run(const LiveTestEnv* env) {
    const uint8_t addr7 = env->addr7;
    const volatile bool* stop = env->stop;
    const LiveTestI2c* i2c = env->i2c;
    const LiveTestPublish publish = env->publish;
    void* const ctx = env->ctx;

    while(!*stop) {
        LiveTestState st;
        memset(&st, 0, sizeof(st));
        st.phase = LiveTestPhaseStarting;
        snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Reading the compass ID");
        publish(ctx, &st);

        const LiveTestIdResult id_seen = ak_identify(i2c, addr7);
        if(id_seen != LiveTestIdMatch) {
            memset(&st, 0, sizeof(st));
            // Worded for both halves of what got us here: an ID that read back
            // wrong, and an ID that could not be read at all. 0x0D also holds
            // the QMC5883L, so the wrong module really is the likely case.
            st.phase = id_seen == LiveTestIdMismatch ? LiveTestPhaseWrongChip : LiveTestPhaseLost;
            if(id_seen == LiveTestIdMismatch) {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "0x%02X answers, but not", addr7);
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "the way an AK09911 does.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It replied, then stopped.");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check RST, 3V3 and wires.");
            }
            publish(ctx, &st);
            ak_delay(
                stop,
                id_seen == LiveTestIdMismatch ? LIVE_TEST_WRONG_CHIP_RETRY_MS :
                                                LIVE_TEST_RETRY_MS);
            continue;
        }

        // From here on the part has been identified and every path below writes
        // CNTL2, so there is unconditionally something to put back -- including
        // when a write was not acknowledged, because one that was not
        // acknowledged may still have landed.

        // --- Factory sensitivity ---------------------------------------
        // Fuse ROM access, three bytes, straight back to power-down. A part
        // that will not give them up is measured without the correction and
        // says so; the issue this test came from recorded a board whose CNTL2
        // read back 0x00 while in fuse-ROM mode, so the readback is not
        // treated as evidence either way.
        uint8_t asa[3] = {0};
        bool have_asa = false;
        if(ak_set_mode(i2c, addr7, stop, AK_MODE_FUSE_ROM)) {
            have_asa = i2c->read_mem(addr7, AK_REG_ASAX, asa, sizeof(asa), LIVE_TEST_TIMEOUT_MS);
            // 0x00 and 0xFF are what an unprogrammed or absent fuse ROM reads
            // as, and either would scale every later number by nonsense.
            for(uint8_t i = 0; i < 3; i++) {
                if(asa[i] == 0x00 || asa[i] == 0xFF) have_asa = false;
            }
        }
        ak_set_mode(i2c, addr7, stop, AK_MODE_POWERDOWN);
        if(!have_asa) memset(asa, 0, sizeof(asa));

        // --- Self-test --------------------------------------------------
        memset(&st, 0, sizeof(st));
        st.phase = LiveTestPhaseStarting;
        st.progress_max = AK_PROOF_STEPS;
        snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Running the self-test");
        snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Hold it still.");
        publish(ctx, &st);

        // The sequence issue #38 asks for is "enter power-down, clear any
        // previous ready sample through ST2, request 31=10". The middle step
        // is not decoration: DRDY clears on an ST2 read and not on a mode
        // write, so a part that an earlier run left in continuous mode still
        // has an earth-field measurement waiting -- and the first poll after
        // the self-test write would return that, unmeasured by the self-test
        // coil, as the self-test result. The read is discarded and its failure
        // ignored: there may be nothing pending, and whether the bus works is
        // about to be established by the self-test itself.
        uint8_t stale[AK_BURST_LEN] = {0};
        i2c->read_mem(addr7, AK_REG_ST1, stale, sizeof(stale), LIVE_TEST_TIMEOUT_MS);

        int32_t self[3] = {0};
        uint16_t dropped = 0;
        bool self_ok = false;
        bool self_ran = ak_set_mode(i2c, addr7, stop, AK_MODE_SELFTEST) &&
                        ak_read_sample(i2c, addr7, stop, self, &dropped) == AkSampleOk;
        if(self_ran) {
            for(uint8_t axis = 0; axis < 3; axis++) {
                self[axis] = ak_adjust(self[axis], asa[axis]);
            }
            self_ok = self[0] >= -AK_SELFTEST_XY_LIMIT && self[0] <= AK_SELFTEST_XY_LIMIT &&
                      self[1] >= -AK_SELFTEST_XY_LIMIT && self[1] <= AK_SELFTEST_XY_LIMIT &&
                      self[2] >= AK_SELFTEST_Z_MIN && self[2] <= AK_SELFTEST_Z_MAX;
        }

        // --- Continuous measurement ------------------------------------
        bool measuring = ak_set_mode(i2c, addr7, stop, AK_MODE_CONT_10HZ);

        AxisRange range[3];
        for(uint8_t axis = 0; axis < 3; axis++) {
            range[axis] = (AxisRange){.min = INT32_MAX, .max = INT32_MIN};
        }
        uint32_t started = furi_get_tick();
        uint32_t samples = 0;
        uint16_t overflows = 0;
        uint8_t errors = 0;
        bool passed = false;

        while(measuring && !passed && !*stop && errors < 3) {
            int32_t raw[3] = {0};
            const AkSampleResult got = ak_read_sample(i2c, addr7, stop, raw, &dropped);
            if(got == AkSampleOverflow) {
                // Saturated. Somebody is holding a magnet against it, which is
                // a fine thing to be doing and not a reason to give up on the
                // part -- so this does not count towards the error budget and
                // does not go into the ranges either. It does have to reach
                // the screen, though: HOFL stays set for as long as the magnet
                // is there, so skipping the publish froze the display on the
                // last good frame and the advice to back off never appeared.
                overflows++;
                memset(&st, 0, sizeof(st));
                st.phase = LiveTestPhaseRunning;
                st.progress = self_ok ? 1 : 0;
                st.progress_max = AK_PROOF_STEPS;
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Too strong - back it off");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Saturated x%u", (unsigned)overflows);
                publish(ctx, &st);
                continue;
            }
            if(got != AkSampleOk) {
                errors++;
                continue;
            }
            errors = 0;
            samples++;

            uint8_t moved = 0;
            for(uint8_t axis = 0; axis < 3; axis++) {
                if(raw[axis] < range[axis].min) range[axis].min = raw[axis];
                if(raw[axis] > range[axis].max) range[axis].max = raw[axis];
                if(range[axis].max - range[axis].min >= AK_MOVE_COUNTS) moved++;
            }
            passed = self_ok && moved >= AK_MOVE_AXES;
            if(passed) break; // publish it after the part is parked, not before

            const float field =
                AK_UT_PER_LSB * sqrtf(
                                    (float)ak_adjust(raw[0], asa[0]) * ak_adjust(raw[0], asa[0]) +
                                    (float)ak_adjust(raw[1], asa[1]) * ak_adjust(raw[1], asa[1]) +
                                    (float)ak_adjust(raw[2], asa[2]) * ak_adjust(raw[2], asa[2]));

            memset(&st, 0, sizeof(st));
            st.phase = LiveTestPhaseRunning;
            st.value = field;
            st.progress = self_ok ? 1 : 0;
            st.progress_max = AK_PROOF_STEPS;
            snprintf(st.heading, sizeof(st.heading), "%u", (unsigned)field);
            snprintf(st.unit, sizeof(st.unit), "uT");
            // Two lines, not three. With a big heading and the progress
            // boxes the generic screen has room for exactly two: it starts at
            // y=46, steps 9 and stops at 55, so a third is composed and then
            // silently dropped. That is how this test shipped -- the progress
            // count and the saturation warning were both written to lines[2]
            // and neither was ever drawn, which left the screen showing raw
            // counts and no hint that the user was meant to do anything.
            //
            // So line 0 is the instruction, carrying its own feedback the way
            // the ADXL345 test's "Gravity on X - tip it" does, and line 1 is
            // the evidence. Raw XYZ came out: the heading is the same field in
            // uT and it moves, and "1/2 axes" says what the triple was there
            // to say in the form the user can act on.
            snprintf(
                st.lines[0], LIVE_TEST_LINE_LEN, "Turn it over - %u/%u axes", moved, AK_MOVE_AXES);
            snprintf(
                st.lines[1],
                LIVE_TEST_LINE_LEN,
                self_ran ? (self_ok ? "Self-test OK    %lus" : "Self-test FAIL  %lus") :
                           "No self-test    %lus",
                (unsigned long)((furi_get_tick() - started) / furi_ms_to_ticks(1000)));
            publish(ctx, &st);
        }

        // --- Park it ----------------------------------------------------
        // Before anything is published, so that the chime the app plays on a
        // pass lands after the last measurement rather than during one: this
        // is a magnetometer, and the speaker sits next to it.
        const bool parked = ak_set_mode(i2c, addr7, stop, AK_MODE_POWERDOWN);

        if(*stop) break;

        memset(&st, 0, sizeof(st));
        st.progress_max = AK_PROOF_STEPS;
        if(passed) {
            st.phase = LiveTestPhasePassed;
            st.progress = AK_PROOF_STEPS;
            snprintf(st.heading, sizeof(st.heading), "%lu", (unsigned long)samples);
            snprintf(st.unit, sizeof(st.unit), "reads");
            // Same two-line budget as the running screen, and "not parked"
            // is the half worth keeping: a part left in continuous mode goes
            // on drawing current and the next run inherits it. The dropped
            // count was diagnostic and went with the third line.
            if(parked) {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Self-test passed, and");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "the field followed you.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Passed, but the part was");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "left configured. Repower.");
            }
            publish(ctx, &st);
            // The screen belongs to the user now. Nothing is being measured
            // and nothing is configured, so this is a plain wait for Back.
            while(!*stop)
                ak_delay(stop, AK_POLL_MS);
            break;
        }

        st.phase = LiveTestPhaseLost;
        st.progress = self_ok ? 1 : 0;
        if(!measuring) {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It will not start");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "measuring. Check 3V3.");
        } else {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It stopped answering.");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check RST, 3V3 and wires.");
        }
        publish(ctx, &st);
        ak_delay(stop, LIVE_TEST_RETRY_MS);
    }
}

const LiveTest live_test_ak09911 = {
    .chip = "AK09911",
    .title = "AK09911 test",
    .offer = "Wave it through a field",
    .addrs = {0x0C, 0x0D},
    .run = ak_run,
    .draw = NULL,
};
