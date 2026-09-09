#include "live_qmc5883p.h"

#include <furi.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// QST QMC5883P, a 3-axis magnetometer. Every constant below is quoted from the
// QMC5883P datasheet, QST document 13-52-19 Rev. C, with the section and page
// named at each one. That document was obtained in full, which is the
// difference between this file and live_ak09911.c next to it.
//
// The part is worth a test for a reason that has nothing to do with its
// registers. GY-271 is the module name the Honeywell HMC5883L made famous, and
// a GY-271 bought today usually has a QST die on it -- an L or, increasingly, a
// P. The three are not register-compatible with each other. So the number
// silkscreened on the board says almost nothing, the chip ID says which die is
// there, and this test says whether that die does the job. On 9 Sep 2026 a blue
// GY-271 on the bench answered at 0x2C and read 0x80 at register 0x00, which is
// what prompted the file.
//
// Run on that board the same day, and it passed: the coil fired, the field
// followed the board through the earth's, 1036 reads. A second entry into the
// same screen did not, and the two faults it exposed are fixed above -- the
// self-test no longer waits for a DRDY the part has stopped producing, and the
// measurement configuration is established from a soft reset instead of
// written on top of whatever the previous run left. That fix has not itself
// been back on silicon. See BACKLOG.md.

// Rev. C section 9.2.1, page 14: "Register 00H stores the chip ID. The default
// value is 80H." One byte, and the only fixed thing in the map -- which makes
// it a weak signature on its own, so it is read twice through
// live_test_read_id8 and nothing is written until it agrees with itself.
#define QMC_REG_CHIPID 0x00
#define QMC_CHIPID     0x80

// Rev. C Table 14 and Table 15, page 14: 01H..06H are X/Y/Z as LSB then MSB,
// "16-bit data width in 2's complement". Read as one six-byte burst.
#define QMC_REG_XOUT_LSB 0x01
#define QMC_DATA_LEN     6

// Rev. C section 9.2.2 and Table 16, page 14: status register 09H, bit 0 DRDY
// "0: no new data, 1: new data is ready", bit 1 OVFL "set high when either axis
// code output exceeds the range of [-30000,30000] LSB". Both are "reset to 0
// after this bit is read", so one read of 09H consumes both flags and the data
// registers must be read before asking again.
#define QMC_REG_STATUS 0x09
#define QMC_ST_DRDY    0x01
#define QMC_ST_OVFL    0x02

// Rev. C Table 17 and Table 18, page 15 and 16. Control register 1 at 0AH holds
// OSR2<7:6>, OSR1<5:4>, ODR<3:2> and MODE<1:0>; control register 2 at 0BH holds
// SOFT_RST<7>, SELF_TEST<6>, RNG<3:2> and SET/RESET MODE<1:0>.
#define QMC_REG_CTRL1 0x0A
#define QMC_REG_CTRL2 0x0B

// The datasheet's own worked examples, quoted whole rather than assembled from
// bit fields -- a byte that the manufacturer has written down and a byte that
// happens to have the right bits in it are not the same kind of evidence.
//
// Rev. C section 7.2, page 11, "Continuous Mode Setup Example":
//   Write Register 29H by 0x06 (Define the sign for X Y and Z axis)
//   Write Register 0BH by 0x08 (Define Set/Reset mode, with Set/Reset On,
//                               Field Range 8Guass)
//   Write Register 0AH by 0xC3 (set continuous mode)
#define QMC_REG_SIGN       0x29
#define QMC_SIGN_VALUE     0x06
#define QMC_CTRL2_RUN      0x08
#define QMC_CTRL1_CONT     0xC3
// Rev. C section 7.3, page 11, "Self-test Example": continuous mode is entered
// with 0x03 and no 0BH write at all, so the range stays at its reset value.
#define QMC_CTRL1_SELFRUN  0x03
// Rev. C section 7.3: "Write Register 0BH by 0x40 (enter self-test function)".
// Table 18: SELF_TEST is "1: self_test enable, auto clear after the data is
// updated", and section 6.2.3 adds that the self-test "can only be enabled in
// Continuous Mode and enters in Suspend Mode after the data is updated".
#define QMC_CTRL2_SELFTEST 0x40
// Rev. C section 7.6, page 11, "Soft Reset Example: Write Register 0BH by
// 0x80", and Table 18: "1: Soft reset, restore default value of all registers".
// One write undoes the sign register, both control registers and the mode --
// which is why parking this part needs no bookkeeping about what was changed.
#define QMC_CTRL2_SOFTRST  0x80
// Rev. C section 7.4: "Suspend Mode Example: Write Register 0AH by 0x00".
#define QMC_CTRL1_SUSPEND  0x00

// Rev. C section 7.3: "Waiting 5 millisecond until measurement ends" between
// arming the self-test and reading the second sample. Ten is double it, and the
// only cost of being generous here is ten milliseconds once per run.
#define QMC_SELFTEST_MS 10

// Rev. C Table 17, page 15: ODR is bits 3:2 of the byte section 7.2 writes to
// 0AH, and 0xC3 selects 10 Hz -- one new sample every hundred milliseconds.
// Reading faster than that re-reads the sample already on the screen, and
// publishing it again drives the display, and anything watching the USB screen
// stream, at whatever rate the I2C bus happens to run at rather than at the
// rate the part measures. So the loop waits a period between samples.
#define QMC_SAMPLE_PERIOD_MS 100

// Rev. C Table 2, page 8: sensitivity is 1000 LSB/G at the +/-30 G range and
// 3750 LSB/G at +/-8 G. The self-test runs at the reset range because the
// datasheet's example does; the measurement phase runs at +/-8 G because its
// example does, and because four times the counts per gauss is four times the
// evidence when the whole signal is the earth.
#define QMC_LSB_PER_G_RESET 1000
#define QMC_LSB_PER_G_RUN   3750

// One gauss is a hundred microtesla, and microtesla is what the heading shows:
// the earth reads about 50 of them, which is a number somebody can sanity-check
// against the AK09911 test on the same bench.
#define QMC_UT_PER_G 100.0f

// Rev. C Table 2, page 8, "Field Resolution, standard deviation": 2 mGauss on
// X and Y, 3 mGauss on Z. At the reset range's 1000 LSB/G that is 2, 2 and 3
// counts of noise, so these are three sigma.
//
// This is a floor, not the specification. Section 9.2.3 says the self-test
// "generat[es] a difference in the 3 axis' value" and that the "user should
// record the value before and after the self-test and compare with threshold
// value" -- and then publishes no threshold, anywhere in the document. So this
// test cannot check the size of the deflection against what QST expects, and it
// does not pretend to: it checks that a deflection happened at all, on every
// axis, by a margin the part's own published noise cannot produce. A canned
// register or a die that ignores bit 6 of 0BH gives zero. A working part gives
// far more than nine counts, and if some part somewhere does not, the honest
// fix is to obtain the threshold from QST rather than to lower these.
#define QMC_SELFTEST_MIN_XY 6
#define QMC_SELFTEST_MIN_Z  9

// ponytail: derived from the datasheet rather than measured, which is exactly
// the footing that made the AK09911 threshold wrong the first time -- so here
// is the arithmetic, to be checked against a still part before it is trusted.
//
// The earth's field is 0.25 to 0.65 gauss depending on where you stand. At the
// +/-8 G range's 3750 LSB/G that is 937 counts at the weakest place on earth,
// and turning a board exactly end over end swings one axis by twice the
// component along it -- so 1875 counts is the worst-case ceiling, not the
// expectation. Three hundred is sixteen per cent of that ceiling, and forty
// times the 7.5 counts of noise the same table implies at this range.
//
// The lesson from live_ak09911.c applies here and has not been paid yet:
// measure the still board first. A threshold means nothing until the noise
// floor under it is known, and this one has never been compared to one.
#define QMC_MOVE_COUNTS 300
#define QMC_MOVE_AXES   2

// Two independent things, and neither implies the other: the part's own coil
// deflects all three channels, and the field those channels report afterwards
// follows the board through the earth's. A canned answer fails the second, a
// dead channel fails the first.
#define QMC_PROOF_STEPS 2

// Continuous mode "runs all the time without sleep time" (section 6.2.3), so a
// sample is never far away. Poll at 20 ms and give up after 500, which is
// twenty-five polls -- long enough that a slow part is waited for, short enough
// that a stopped one is noticed.
#define QMC_POLL_MS           20
#define QMC_SAMPLE_TIMEOUT_MS 500

typedef struct {
    int32_t min;
    int32_t max;
} QmcAxisRange;

typedef enum {
    QmcSampleOk,
    QmcSampleOverflow, // past +/-30000 LSB: not a small reading, not a reading
    QmcSampleFailed, // the bus failed, or nothing became ready in time
} QmcSampleResult;

static void qmc_delay(const volatile bool* stop, uint32_t ms) {
    while(ms && !*stop) {
        uint32_t chunk = ms > 40 ? 40 : ms;
        furi_delay_ms(chunk);
        ms -= chunk;
    }
}

// Section 7.4's Suspend Mode example, used as the first step of every
// configuration here. Section 9.2.3: "Suspend Mode should be added in the
// middle of mode shifting between Continuous Mode, Single Mode and Normal
// Mode." So the mode the next write starts from is established rather than
// inherited from whatever the last pass left behind.
//
// This is not tidiness. On silicon the first run of this test passed and the
// second did not, because the second began on a part still carrying the first
// run's state. Section 7.6's soft reset was tried here first and is worse for
// the job: it does more, but the datasheet gives no settling time for it
// anywhere, and a configuration written into a part that is still resetting is
// the same fault wearing a different hat. One write to 0AH needs no such
// guess.
static bool qmc_suspend(const LiveTestI2c* i2c, uint8_t addr7) {
    return i2c->write_reg(addr7, QMC_REG_CTRL1, QMC_CTRL1_SUSPEND, LIVE_TEST_TIMEOUT_MS);
}

static LiveTestIdResult qmc_identify(const LiveTestI2c* i2c, uint8_t addr7) {
    uint8_t id = 0;
    if(!live_test_read_id8(i2c, addr7, QMC_REG_CHIPID, &id))
        return live_test_id_unreadable(i2c, addr7);
    return id == QMC_CHIPID ? LiveTestIdMatch : LiveTestIdMismatch;
}

// The six data bytes, assembled. Table 15 has them LSB register first then MSB
// for each axis, "16-bit data width in 2's complement". Getting that backwards
// produces numbers that look entirely plausible and are not.
static bool qmc_read_data(const LiveTestI2c* i2c, uint8_t addr7, int32_t out[3]) {
    uint8_t buf[QMC_DATA_LEN] = {0};
    if(!i2c->read_mem(addr7, QMC_REG_XOUT_LSB, buf, sizeof(buf), LIVE_TEST_TIMEOUT_MS))
        return false;
    for(uint8_t axis = 0; axis < 3; axis++) {
        out[axis] = (int16_t)((uint16_t)buf[axis * 2] | ((uint16_t)buf[axis * 2 + 1] << 8));
    }
    return true;
}

// One measurement: wait for DRDY in 09H, then read the six data bytes. The
// status read is what clears both flags, so it happens once per attempt and its
// result is carried out rather than asked for again.
//
// An overflowed sample is reported as such and its bytes are never returned.
// Printing them would be printing a number the part did not stand behind, and
// past 30000 LSB the axis has run out of range rather than measured anything.
static QmcSampleResult qmc_read_sample(
    const LiveTestI2c* i2c,
    uint8_t addr7,
    const volatile bool* stop,
    int32_t out[3]) {
    for(uint32_t waited = 0; waited < QMC_SAMPLE_TIMEOUT_MS && !*stop; waited += QMC_POLL_MS) {
        uint8_t status = 0;
        if(!i2c->read_reg(addr7, QMC_REG_STATUS, &status, LIVE_TEST_TIMEOUT_MS))
            return QmcSampleFailed;

        if(!(status & QMC_ST_DRDY)) {
            qmc_delay(stop, QMC_POLL_MS);
            continue;
        }
        if(status & QMC_ST_OVFL) return QmcSampleOverflow;

        if(!qmc_read_data(i2c, addr7, out)) return QmcSampleFailed;
        return QmcSampleOk;
    }
    return QmcSampleFailed;
}

// The datasheet's section 7.3 in order, with the two reads it asks for. The
// deltas it defines are (x1-x2), (y2-y1) and (z2-z1) -- written with different
// signs per axis because the internal coil pushes X one way and Y and Z the
// other. Only the size of each is checked here; see QMC_SELFTEST_MIN_XY for why
// the direction is not.
static bool qmc_self_test(
    const LiveTestI2c* i2c,
    uint8_t addr7,
    const volatile bool* stop,
    int32_t delta[3]) {
    if(!qmc_suspend(i2c, addr7)) return false;
    if(!i2c->write_reg(addr7, QMC_REG_SIGN, QMC_SIGN_VALUE, LIVE_TEST_TIMEOUT_MS)) return false;
    if(!i2c->write_reg(addr7, QMC_REG_CTRL1, QMC_CTRL1_SELFRUN, LIVE_TEST_TIMEOUT_MS))
        return false;

    int32_t before[3] = {0};
    if(qmc_read_sample(i2c, addr7, stop, before) != QmcSampleOk) return false;

    if(!i2c->write_reg(addr7, QMC_REG_CTRL2, QMC_CTRL2_SELFTEST, LIVE_TEST_TIMEOUT_MS))
        return false;
    qmc_delay(stop, QMC_SELFTEST_MS);

    // Section 7.3 waits and then reads, with no second look at 09H: "Waiting 5
    // millisecond until measurement ends / Read data Register 01H ~ 06H".
    // Section 6.2.3 says why that is not an oversight -- the self-test "can
    // only be enabled in Continuous Mode and enters in Suspend Mode after the
    // data is updated", so a part that has finished is a part that has stopped,
    // and DRDY is a flag on a sample that is not coming. Waiting for it here is
    // what made the second run on silicon report "No self-test" from a part
    // that had just done one.
    int32_t after[3] = {0};
    if(!qmc_read_data(i2c, addr7, after)) return false;

    delta[0] = before[0] - after[0];
    delta[1] = after[1] - before[1];
    delta[2] = after[2] - before[2];
    return true;
}

static bool qmc_self_test_passed(const int32_t delta[3]) {
    const int32_t x = delta[0] < 0 ? -delta[0] : delta[0];
    const int32_t y = delta[1] < 0 ? -delta[1] : delta[1];
    const int32_t z = delta[2] < 0 ? -delta[2] : delta[2];
    return x >= QMC_SELFTEST_MIN_XY && y >= QMC_SELFTEST_MIN_XY && z >= QMC_SELFTEST_MIN_Z;
}

static void qmc_run(const LiveTestEnv* env) {
    const uint8_t addr7 = env->addr7;
    const volatile bool* stop = env->stop;
    const LiveTestI2c* i2c = env->i2c;
    const LiveTestPublish publish = env->publish;
    void* const ctx = env->ctx;

    while(!*stop) {
        LiveTestState st;
        memset(&st, 0, sizeof(st));
        st.phase = LiveTestPhaseStarting;
        snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Reading the chip ID");
        publish(ctx, &st);

        const LiveTestIdResult id_seen = qmc_identify(i2c, addr7);
        if(id_seen != LiveTestIdMatch) {
            memset(&st, 0, sizeof(st));
            st.phase = id_seen == LiveTestIdMismatch ? LiveTestPhaseWrongChip : LiveTestPhaseLost;
            if(id_seen == LiveTestIdMismatch) {
                // The likely case by some distance. A GY-271 board can carry an
                // HMC5883L, a QMC5883L or this, and only this one sits at 0x2C.
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "0x%02X answers, but not", addr7);
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "the way a QMC5883P does.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It replied, then stopped.");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check 3V3 and the wires.");
            }
            publish(ctx, &st);
            qmc_delay(
                stop,
                id_seen == LiveTestIdMismatch ? LIVE_TEST_WRONG_CHIP_RETRY_MS :
                                                LIVE_TEST_RETRY_MS);
            continue;
        }

        // Identified. From here every path has written something, so every path
        // out of this loop body soft-resets the part -- including the ones where
        // a write was not acknowledged, because a write that was not
        // acknowledged may still have landed.

        // --- Self-test --------------------------------------------------
        memset(&st, 0, sizeof(st));
        st.phase = LiveTestPhaseStarting;
        st.progress_max = QMC_PROOF_STEPS;
        snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Firing the test coil");
        snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Hold it still.");
        publish(ctx, &st);

        int32_t delta[3] = {0};
        const bool self_ran = qmc_self_test(i2c, addr7, stop, delta);
        const bool self_ok = self_ran && qmc_self_test_passed(delta);

        // --- Continuous measurement -------------------------------------
        // Section 6.2.3 has the self-test drop the part into Suspend Mode when
        // it finishes -- but only when it finishes, and the self-test above is
        // allowed to fail halfway with 0BH already written. So the mode is
        // established rather than assumed, and then section 7.2's example
        // follows, all three writes, in its order.
        const bool measuring =
            qmc_suspend(i2c, addr7) &&
            i2c->write_reg(addr7, QMC_REG_SIGN, QMC_SIGN_VALUE, LIVE_TEST_TIMEOUT_MS) &&
            i2c->write_reg(addr7, QMC_REG_CTRL2, QMC_CTRL2_RUN, LIVE_TEST_TIMEOUT_MS) &&
            i2c->write_reg(addr7, QMC_REG_CTRL1, QMC_CTRL1_CONT, LIVE_TEST_TIMEOUT_MS);

        QmcAxisRange range[3];
        for(uint8_t axis = 0; axis < 3; axis++) {
            range[axis] = (QmcAxisRange){.min = INT32_MAX, .max = INT32_MIN};
        }
        const uint32_t started = furi_get_tick();
        uint32_t samples = 0;
        uint16_t overflows = 0;
        uint8_t errors = 0;
        bool passed = false;

        while(measuring && !passed && !*stop && errors < 3) {
            int32_t raw[3] = {0};
            const QmcSampleResult got = qmc_read_sample(i2c, addr7, stop, raw);
            if(got == QmcSampleOverflow) {
                // A magnet against the case. Not a fault and not an error
                // budget item, but it does have to reach the screen: OVFL
                // clears on the read that saw it, so without a publish here the
                // display would simply stop moving with no reason given.
                overflows++;
                memset(&st, 0, sizeof(st));
                st.phase = LiveTestPhaseRunning;
                st.progress = self_ok ? 1 : 0;
                st.progress_max = QMC_PROOF_STEPS;
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Too strong - back it off");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Saturated x%u", (unsigned)overflows);
                publish(ctx, &st);
                qmc_delay(stop, QMC_SAMPLE_PERIOD_MS);
                continue;
            }
            if(got != QmcSampleOk) {
                errors++;
                continue;
            }
            errors = 0;
            samples++;

            uint8_t moved = 0;
            for(uint8_t axis = 0; axis < 3; axis++) {
                if(raw[axis] < range[axis].min) range[axis].min = raw[axis];
                if(raw[axis] > range[axis].max) range[axis].max = raw[axis];
                if(range[axis].max - range[axis].min >= QMC_MOVE_COUNTS) moved++;
            }
            passed = self_ok && moved >= QMC_MOVE_AXES;
            if(passed) break; // publish it after the part is parked, not before

            const float field =
                QMC_UT_PER_G *
                sqrtf((float)raw[0] * raw[0] + (float)raw[1] * raw[1] + (float)raw[2] * raw[2]) /
                QMC_LSB_PER_G_RUN;

            memset(&st, 0, sizeof(st));
            st.phase = LiveTestPhaseRunning;
            st.value = field;
            st.progress = self_ok ? 1 : 0;
            st.progress_max = QMC_PROOF_STEPS;
            snprintf(st.heading, sizeof(st.heading), "%u", (unsigned)field);
            snprintf(st.unit, sizeof(st.unit), "uT");
            // Two lines, because a heading and progress boxes leave room for
            // exactly two. Line 0 is the instruction and carries its own
            // feedback; line 1 is what the coil said, which is the half the
            // user cannot see happening.
            snprintf(
                st.lines[0], LIVE_TEST_LINE_LEN, "Turn it over - %u/%u axes", moved, QMC_MOVE_AXES);
            snprintf(
                st.lines[1],
                LIVE_TEST_LINE_LEN,
                self_ran ? (self_ok ? "Coil OK         %lus" : "Coil weak       %lus") :
                           "No self-test    %lus",
                (unsigned long)((furi_get_tick() - started) / furi_ms_to_ticks(1000)));
            publish(ctx, &st);
            qmc_delay(stop, QMC_SAMPLE_PERIOD_MS);
        }

        // --- Park it ----------------------------------------------------
        // One write puts everything back: section 7.6's soft reset restores the
        // default value of all registers, which includes the sign register this
        // test wrote and the Suspend Mode the part powers up in. Done before
        // anything is published so the pass chime lands after the last
        // measurement rather than during one -- this is a magnetometer and the
        // speaker is a centimetre away.
        const bool parked =
            i2c->write_reg(addr7, QMC_REG_CTRL2, QMC_CTRL2_SOFTRST, LIVE_TEST_TIMEOUT_MS);

        if(*stop) break;

        memset(&st, 0, sizeof(st));
        st.progress_max = QMC_PROOF_STEPS;
        if(passed) {
            st.phase = LiveTestPhasePassed;
            st.progress = QMC_PROOF_STEPS;
            snprintf(st.heading, sizeof(st.heading), "%lu", (unsigned long)samples);
            snprintf(st.unit, sizeof(st.unit), "reads");
            if(parked) {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "The coil fired and the");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "field followed you.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Passed, but the part was");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "left running. Repower.");
            }
            publish(ctx, &st);
            // Nothing is being measured and nothing is configured; this is a
            // plain wait for Back.
            while(!*stop)
                qmc_delay(stop, QMC_POLL_MS);
            break;
        }

        st.phase = LiveTestPhaseLost;
        st.progress = self_ok ? 1 : 0;
        if(!measuring) {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It will not start");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "measuring. Check 3V3.");
        } else {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It stopped answering.");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check 3V3 and the wires.");
        }
        publish(ctx, &st);
        qmc_delay(stop, LIVE_TEST_RETRY_MS);
    }
}

const LiveTest live_test_qmc5883p = {
    .chip = "QMC5883P",
    .title = "QMC5883P test",
    .offer = "Turn it through a field",
    .addrs = {0x2C},
    .run = qmc_run,
    .draw = NULL,
};
