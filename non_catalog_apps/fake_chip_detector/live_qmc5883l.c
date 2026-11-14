#include "live_qmc5883l.h"

#include <furi.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

// QST QMC5883L, a 3-axis magnetometer. Every constant below is quoted from the
// QMC5883L datasheet, QST document 13-52-04, QST-PD-B002-22, Rev. B, with the
// section and page named at each one.
//
// This is the third of the three dies a board silkscreened GY-271 can carry,
// and the one it usually does. The other two already have somewhere to go: the
// QMC5883P sits at 0x2C and has live_qmc5883p.c next to this file, and the
// Honeywell HMC5883L sits at 0x1E and has been out of production since 2016.
// This part answers at 0x0D, which is also where an AK09911 answers -- so the
// chip ID is doing real work here, not ceremony.
//
// The important difference from the P, and the reason this file is shorter:
// the QMC5883L has no self-test. Its control register 2 holds SOFT_RST,
// ROL_PNT and INT_ENB and nothing else (Rev. B section 9.2.4, page 19), so
// there is no internal coil to fire and no way to make the part deflect its
// own channels on command. That leaves one honest proof, and this test makes
// exactly one claim: the field this part reports follows the board through the
// earth's. A canned register cannot do that. A dead channel cannot do that.
// What it does not prove is anything about the signal chain that a still part
// could also satisfy, and the screen does not pretend otherwise.
//
// NOT RUN ON HARDWARE. The sequences are the datasheet's own worked examples
// and it compiles, but no QMC5883L has been on the bench -- the board that
// prompted this work carries a P. See BACKLOG.md.

// Rev. B section 9.2.6 and Table 19, page 20: "This register is chip
// identification register. It returns 0xFF."
//
// That is the weakest possible signature -- 0xFF is also what an idle bus
// looks like -- which is why it is read through live_test_read_id8, twice,
// with the two required to agree, and why nothing is written until they do. It
// is still evidence: a bus with pull-ups and no device at this address NACKs
// rather than answering 0xFF, and the other part that lives at 0x0D, the
// AK09911, answers 0x48 there.
#define QMCL_REG_CHIPID 0x0D
#define QMCL_CHIPID     0xFF

// Rev. B Table 12 and Table 13, pages 18: 00H..05H are X/Y/Z as LSB then MSB,
// "Each axis has 16 bit data width in 2's complement". Read as one six-byte
// burst.
#define QMCL_REG_XOUT_LSB 0x00
#define QMCL_DATA_LEN     6

// Rev. B section 9.2.2 and Table 14, pages 18 and 19: status register 06H, bit
// 0 DRDY "0: no new data, 1: new data is ready", bit 1 OVL "set to 1 if any
// data of three axis magnetic sensor channels is out of range", bit 2 DOR
// "set to 1 if all the channels of output data registers are skipped in
// reading".
//
// DRDY and DOR are "reset to 0 by reading any data register (00H~05H)" -- not
// by reading the status register, which is the opposite of the QMC5883P and
// the reason this loop reads status and data in that order without having to
// carry the flags out. OVL is different again: it "is reset to 0 if next
// measurement goes back to the range", so it clears itself when the magnet
// goes away and never needs acknowledging.
#define QMCL_REG_STATUS 0x06
#define QMCL_ST_DRDY    0x01
#define QMCL_ST_OVL     0x02

// Rev. B Table 12, page 18, and section 9.2.4, page 19. Control register 1 at
// 09H holds OSR<7:6>, RNG<5:4>, ODR<3:2> and MODE<1:0>; control register 2 at
// 0AH holds SOFT_RST<7>, ROL_PNT<6> and INT_ENB<0>; 0BH is the SET/RESET
// period FBR.
#define QMCL_REG_CTRL1 0x09
#define QMCL_REG_CTRL2 0x0A
#define QMCL_REG_FBR   0x0B

// The datasheet's own worked examples, quoted whole rather than assembled from
// bit fields -- a byte the manufacturer has written down and a byte that
// happens to have the right bits in it are not the same kind of evidence.
//
// Rev. B section 7.1, page 15, "Continuous Mode Setup Example":
//   (1) Write Register 0BH by 0x01 (Define Set/Reset period)
//   (2) Write Register 09H by 0x1D (Define OSR = 512, Full Scale Range = 8
//       Gauss, ODR = 200Hz, set continuous measurement mode)
#define QMCL_FBR_VALUE     0x01
#define QMCL_CTRL1_CONT    0x1D
// Rev. B section 7.3, page 15, "Standby Example: Write Register 09H by 0x00".
#define QMCL_CTRL1_STANDBY 0x00
// Rev. B section 7.4, page 15, "Soft Reset Example: Write Register 0AH by
// 0x80", and section 9.2.4: "Soft reset can be invoked at any time of any
// mode... restore default value of all registers". One write undoes both
// control registers, the set/reset period and the mode, which is why parking
// this part needs no bookkeeping about what was changed.
#define QMCL_CTRL2_SOFTRST 0x80

// Rev. B Table 2, page 8: sensitivity is 12000 LSB/G at the +/-2 G range and
// 3000 LSB/G at +/-8 G. The example above selects +/-8 G, so that is the
// divisor every number on this screen goes through.
#define QMCL_LSB_PER_G 3000

// One gauss is a hundred microtesla, and microtesla is what the heading shows:
// the earth reads about 50 of them, which is a number somebody can sanity-check
// against the QMC5883P test on the same bench.
#define QMCL_UT_PER_G 100.0f

// The earth's field is 0.25 to 0.65 gauss depending on where you stand. At
// 3000 LSB/G that is 750 counts at the weakest place on earth, and turning a
// board exactly end over end swings one axis by twice the component along it --
// so 1500 counts is the worst-case ceiling, not the expectation. Two hundred
// and forty is sixteen per cent of that ceiling.
//
// Rev. B Table 2, page 8, "Field Resolution, Standard deviation 100 Data, FS
// +/-2G": 2 mGauss. That is quoted at the other range, but 2 mGauss is 6 counts
// at this one, so the threshold is forty times the part's own published noise
// either way.
//
// ponytail: derived from the datasheet rather than measured, which is exactly
// the footing that made the AK09911 threshold wrong the first time. Measure a
// still part before trusting it; BACKLOG.md says so too.
#define QMCL_MOVE_COUNTS 240
#define QMCL_MOVE_AXES   2

// And a floor under how few samples a pass may be built from. The QMC5883P
// test has none, and on silicon it once passed after two reads -- which is few
// enough that a sensor reconnecting mid-test could have supplied the whole
// swing by itself, with no field and no hand involved. That test has a coil
// firing underneath it; this one has nothing but the movement, so the movement
// has to come from a run of readings rather than from a pair. Eight of them at
// a sample period each is under a second of turning.
#define QMCL_MIN_SAMPLES 8

// Rev. B Table 16, page 19: ODR is bits 3:2 of the byte section 7.1 writes to
// 09H, and 0x1D selects 200 Hz. Polling at 20 ms therefore always finds a
// sample waiting, and 500 ms of nothing means the part has stopped rather than
// that it is slow.
#define QMCL_POLL_MS           20
#define QMCL_SAMPLE_TIMEOUT_MS 500

// And this is why the loop does not read at 200 Hz just because it could. A
// hand cannot turn a board faster than a few times a second, the screen cannot
// show more, and publishing a frame per sample drives the display -- and
// anything watching it over USB -- at a rate nothing asked for. The QMC5883P
// test shipped without this and flooded its own screen stream until USB writes
// timed out. Sampling every hundred milliseconds deliberately skips data,
// which is what the DOR flag exists to announce, so DOR is not read here and
// not reported: it would be reporting this test's own choice back to the user
// as a fault.
#define QMCL_SAMPLE_PERIOD_MS 100

typedef struct {
    int32_t min;
    int32_t max;
} QmclAxisRange;

typedef enum {
    QmclSampleOk,
    QmclSampleOverflow, // an axis is out of range: not a small reading, not a reading
    QmclSampleFailed, // the bus failed, or nothing became ready in time
} QmclSampleResult;

static void qmcl_delay(const volatile bool* stop, uint32_t ms) {
    while(ms && !*stop) {
        uint32_t chunk = ms > 40 ? 40 : ms;
        furi_delay_ms(chunk);
        ms -= chunk;
    }
}

static LiveTestIdResult qmcl_identify(const LiveTestI2c* i2c, uint8_t addr7) {
    uint8_t id = 0;
    if(!live_test_read_id8(i2c, addr7, QMCL_REG_CHIPID, &id))
        return live_test_id_unreadable(i2c, addr7);
    return id == QMCL_CHIPID ? LiveTestIdMatch : LiveTestIdMismatch;
}

// One measurement: section 7.2's two steps, page 15 -- "Check status register
// 06H[0], 1 means ready" and "Read data register 00H ~ 05H".
//
// An overflowed sample is reported as such and its bytes are never returned.
// Printing them would be printing a number the part did not stand behind, and
// past the saturation point the axis has run out of range rather than measured
// anything.
static QmclSampleResult qmcl_read_sample(
    const LiveTestI2c* i2c,
    uint8_t addr7,
    const volatile bool* stop,
    int32_t out[3]) {
    for(uint32_t waited = 0; waited < QMCL_SAMPLE_TIMEOUT_MS && !*stop; waited += QMCL_POLL_MS) {
        uint8_t status = 0;
        if(!i2c->read_reg(addr7, QMCL_REG_STATUS, &status, LIVE_TEST_TIMEOUT_MS))
            return QmclSampleFailed;

        if(!(status & QMCL_ST_DRDY)) {
            qmcl_delay(stop, QMCL_POLL_MS);
            continue;
        }
        if(status & QMCL_ST_OVL) return QmclSampleOverflow;

        uint8_t buf[QMCL_DATA_LEN] = {0};
        if(!i2c->read_mem(addr7, QMCL_REG_XOUT_LSB, buf, sizeof(buf), LIVE_TEST_TIMEOUT_MS))
            return QmclSampleFailed;

        for(uint8_t axis = 0; axis < 3; axis++) {
            // LSB register first, then MSB -- Table 13. Getting this backwards
            // produces numbers that look entirely plausible and are not.
            out[axis] = (int16_t)((uint16_t)buf[axis * 2] | ((uint16_t)buf[axis * 2 + 1] << 8));
        }
        return QmclSampleOk;
    }
    return QmclSampleFailed;
}

static void qmcl_run(const LiveTestEnv* env) {
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

        const LiveTestIdResult id_seen = qmcl_identify(i2c, addr7);
        if(id_seen != LiveTestIdMatch) {
            memset(&st, 0, sizeof(st));
            st.phase = id_seen == LiveTestIdMismatch ? LiveTestPhaseWrongChip : LiveTestPhaseLost;
            if(id_seen == LiveTestIdMismatch) {
                // 0x0D is shared. An AK09911 answers here too, and so does a
                // GY-271 whose die is something this app has never met.
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "0x%02X answers, but not", addr7);
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "the way a QMC5883L does.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It replied, then stopped.");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check 3V3 and the wires.");
            }
            publish(ctx, &st);
            qmcl_delay(
                stop,
                id_seen == LiveTestIdMismatch ? LIVE_TEST_WRONG_CHIP_RETRY_MS :
                                                LIVE_TEST_RETRY_MS);
            continue;
        }

        // Identified. From here every path has written something, so every path
        // out of this loop body soft-resets the part -- including the ones where
        // a write was not acknowledged, because a write that was not
        // acknowledged may still have landed.

        memset(&st, 0, sizeof(st));
        st.phase = LiveTestPhaseStarting;
        st.progress_max = QMCL_MOVE_AXES;
        snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Starting the magnetometer");
        publish(ctx, &st);

        // Section 7.3's Standby first, so the configuration below is written
        // into a mode this test established rather than into whatever the last
        // pass left behind. Section 9.2.4 says there is "no any restriction in
        // the transferring between the modes", so this is not required the way
        // the QMC5883P's is -- it is one write, and it makes the next two mean
        // the same thing on the second run as on the first.
        //
        // Then section 7.1's example, both writes, in its order.
        const bool measuring =
            i2c->write_reg(addr7, QMCL_REG_CTRL1, QMCL_CTRL1_STANDBY, LIVE_TEST_TIMEOUT_MS) &&
            i2c->write_reg(addr7, QMCL_REG_FBR, QMCL_FBR_VALUE, LIVE_TEST_TIMEOUT_MS) &&
            i2c->write_reg(addr7, QMCL_REG_CTRL1, QMCL_CTRL1_CONT, LIVE_TEST_TIMEOUT_MS);

        QmclAxisRange range[3];
        for(uint8_t axis = 0; axis < 3; axis++) {
            range[axis] = (QmclAxisRange){.min = INT32_MAX, .max = INT32_MIN};
        }
        uint32_t samples = 0;
        uint16_t overflows = 0;
        uint8_t errors = 0;
        bool passed = false;

        while(measuring && !passed && !*stop && errors < 3) {
            int32_t raw[3] = {0};
            const QmclSampleResult got = qmcl_read_sample(i2c, addr7, stop, raw);
            if(got == QmclSampleOverflow) {
                // A magnet against the case. Not a fault and not an error
                // budget item, but it does have to reach the screen: without a
                // publish here the display would simply stop moving with no
                // reason given.
                overflows++;
                memset(&st, 0, sizeof(st));
                st.phase = LiveTestPhaseRunning;
                st.progress_max = QMCL_MOVE_AXES;
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Too strong - back it off");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Saturated x%u", (unsigned)overflows);
                publish(ctx, &st);
                qmcl_delay(stop, QMCL_SAMPLE_PERIOD_MS);
                continue;
            }
            if(got != QmclSampleOk) {
                errors++;
                continue;
            }
            errors = 0;
            samples++;

            uint8_t moved = 0;
            for(uint8_t axis = 0; axis < 3; axis++) {
                if(raw[axis] < range[axis].min) range[axis].min = raw[axis];
                if(raw[axis] > range[axis].max) range[axis].max = raw[axis];
                if(range[axis].max - range[axis].min >= QMCL_MOVE_COUNTS) moved++;
            }
            passed = moved >= QMCL_MOVE_AXES && samples >= QMCL_MIN_SAMPLES;
            if(passed) break; // publish it after the part is parked, not before

            const float field =
                QMCL_UT_PER_G *
                sqrtf((float)raw[0] * raw[0] + (float)raw[1] * raw[1] + (float)raw[2] * raw[2]) /
                QMCL_LSB_PER_G;

            memset(&st, 0, sizeof(st));
            st.phase = LiveTestPhaseRunning;
            st.value = field;
            st.progress = moved;
            st.progress_max = QMCL_MOVE_AXES;
            snprintf(st.heading, sizeof(st.heading), "%u", (unsigned)field);
            snprintf(st.unit, sizeof(st.unit), "uT");
            // Two lines, because a heading and progress boxes leave room for
            // exactly two. The boxes already say how many axes have moved, so
            // line 0 is the instruction and line 1 is the evidence that the
            // part is answering at all rather than showing one frozen frame.
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Turn it over slowly");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "%lu reads", (unsigned long)samples);
            publish(ctx, &st);
            qmcl_delay(stop, QMCL_SAMPLE_PERIOD_MS);
        }

        // --- Park it ----------------------------------------------------
        // One write puts everything back: section 7.4's soft reset restores the
        // default value of all registers, and section 9.2.4 adds that the part
        // "immediately switches to standby mode due to mode register is reset
        // to 00 in default". Done before anything is published so the pass
        // chime lands after the last measurement rather than during one -- this
        // is a magnetometer and the speaker is a centimetre away.
        const bool parked =
            i2c->write_reg(addr7, QMCL_REG_CTRL2, QMCL_CTRL2_SOFTRST, LIVE_TEST_TIMEOUT_MS);

        if(*stop) break;

        memset(&st, 0, sizeof(st));
        st.progress_max = QMCL_MOVE_AXES;
        if(passed) {
            st.phase = LiveTestPhasePassed;
            st.progress = QMCL_MOVE_AXES;
            snprintf(st.heading, sizeof(st.heading), "%lu", (unsigned long)samples);
            snprintf(st.unit, sizeof(st.unit), "reads");
            if(parked) {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "The field followed you");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "on two axes.");
            } else {
                snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "Passed, but the part was");
                snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "left running. Repower.");
            }
            publish(ctx, &st);
            // Nothing is being measured and nothing is configured; this is a
            // plain wait for Back.
            while(!*stop)
                qmcl_delay(stop, QMCL_POLL_MS);
            break;
        }

        st.phase = LiveTestPhaseLost;
        if(!measuring) {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It will not start");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "measuring. Check 3V3.");
        } else {
            snprintf(st.lines[0], LIVE_TEST_LINE_LEN, "It stopped answering.");
            snprintf(st.lines[1], LIVE_TEST_LINE_LEN, "Check 3V3 and the wires.");
        }
        publish(ctx, &st);
        qmcl_delay(stop, LIVE_TEST_RETRY_MS);
    }
}

const LiveTest live_test_qmc5883l = {
    .chip = "QMC5883L",
    .title = "QMC5883L test",
    .offer = "Turn it through a field",
    .addrs = {0x0D},
    .run = qmcl_run,
    .draw = NULL,
};
