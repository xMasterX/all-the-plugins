#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/dcf77_util.h"
#include "../src/radio_clock_protocol.h"

static char pulse_to_char(RadioClockPulse pulse) {
    switch(pulse) {
    case RadioClockPulseOne:
        return '1';
    case RadioClockPulseMarker:
        return 'M';
    case RadioClockPulseMsfA0B0:
        return 'a';
    case RadioClockPulseMsfA0B1:
        return 'b';
    case RadioClockPulseMsfA1B0:
        return 'c';
    case RadioClockPulseMsfA1B1:
        return 'd';
    case RadioClockPulsePair00:
        return '0';
    case RadioClockPulsePair01:
        return '1';
    case RadioClockPulsePair10:
        return '2';
    case RadioClockPulsePair11:
        return '3';
    case RadioClockPulseZero:
    default:
        return '0';
    }
}

static uint32_t waveform_total_ticks(const RadioClockSecondWaveform* waveform) {
    uint32_t total = 0U;

    for(uint8_t i = 0; i < waveform->segment_count; i++) {
        total += waveform->segments[i].ticks;
    }

    return total;
}

static void frame_to_string(const RadioClockPulse* frame, char* out) {
    for(size_t i = 0; i < RADIO_CLOCK_FRAME_SECONDS; i++) {
        out[i] = pulse_to_char(frame[i]);
    }
    out[RADIO_CLOCK_FRAME_SECONDS] = '\0';
}

static void test_dcf77_protocol_matches_known_frame(void) {
    RadioClockProtocolTime time = {
        .year = 2026,
        .day_of_year = 5,
        .month = 1,
        .day = 5,
        .weekday = 1,
        .hour = 14,
        .minute = 30,
    };
    RadioClockMinuteFrame frame;
    uint8_t expected[8] = {0};

    radio_clock_protocol_prepare_frame(RadioClockSignalDcf77, &frame, &time);
    set_dcf_message(expected, 30, 14, 5, 1, 26, 1, false, false, false, false, 0x0000);

    assert(memcmp(frame.encoded, expected, sizeof(expected)) == 0);
    for(uint8_t second = 0; second < 59U; second++) {
        const RadioClockPulse expected_pulse =
            dcf77_message_get_bit(expected, second) ? RadioClockPulseOne : RadioClockPulseZero;
        assert(frame.pulses[second] == expected_pulse);
        assert(frame.waveforms[second].segment_count == 2U);
        assert(frame.waveforms[second].segments[0].level == false);
        assert(frame.waveforms[second].segments[1].level == true);
        assert(waveform_total_ticks(&frame.waveforms[second]) == 32768U);
    }
    assert(frame.pulses[59] == RadioClockPulseMarker);
    assert(frame.waveforms[59].segment_count == 1U);
    assert(frame.waveforms[59].segments[0].level == true);
    assert(frame.waveforms[59].segments[0].ticks == 32768U);
}

static void test_wwvb_protocol_uses_flipper_time_with_safe_aux_flags(void) {
    RadioClockProtocolTime time = {
        .year = 2024,
        .day_of_year = 60,
        .month = 2,
        .day = 29,
        .weekday = 4,
        .hour = 23,
        .minute = 59,
    };
    RadioClockMinuteFrame frame;

    radio_clock_protocol_prepare_frame(RadioClockSignalWwvb, &frame, &time);

    assert(frame.pulses[0] == RadioClockPulseMarker);
    assert(frame.pulses[9] == RadioClockPulseMarker);
    assert(frame.pulses[59] == RadioClockPulseMarker);
    assert(frame.pulses[55] == RadioClockPulseOne);
    assert(frame.pulses[56] == RadioClockPulseZero);
    assert(frame.pulses[57] == RadioClockPulseZero);
    assert(frame.pulses[58] == RadioClockPulseZero);
    assert(frame.pulses[36] == RadioClockPulseZero);
    assert(frame.pulses[37] == RadioClockPulseZero);
    assert(frame.pulses[38] == RadioClockPulseZero);
    assert(frame.pulses[40] == RadioClockPulseZero);
    assert(frame.pulses[41] == RadioClockPulseZero);
    assert(frame.pulses[42] == RadioClockPulseZero);
    assert(frame.pulses[43] == RadioClockPulseZero);
}

static void test_wwvb_protocol_generates_safe_aux_frame(void) {
    RadioClockProtocolTime time = {
        .year = 2001,
        .day_of_year = 258,
        .month = 9,
        .day = 15,
        .weekday = 6,
        .hour = 18,
        .minute = 42,
    };
    RadioClockMinuteFrame frame;
    char actual[RADIO_CLOCK_FRAME_SECONDS + 1];

    radio_clock_protocol_prepare_frame(RadioClockSignalWwvb, &frame, &time);
    frame_to_string(frame.pulses, actual);

    assert(
        strcmp(
            actual,
            "M10000010M000101000M001000101M100000000M000000000M000100000M") == 0);
}

static void test_protocol_pulse_timing_and_phase_rules(void) {
    RadioClockProtocolTime dcf_time = {
        .year = 2026,
        .day_of_year = 5,
        .month = 1,
        .day = 5,
        .weekday = 1,
        .hour = 14,
        .minute = 30,
    };
    RadioClockProtocolTime wwvb_time = {
        .year = 2001,
        .day_of_year = 258,
        .month = 9,
        .day = 15,
        .weekday = 6,
        .hour = 18,
        .minute = 42,
    };
    RadioClockProtocolTime bsf_time = {
        .year = 2009,
        .day_of_year = 271,
        .month = 9,
        .day = 28,
        .weekday = 1,
        .hour = 9,
        .minute = 16,
    };
    RadioClockMinuteFrame bsf_frame;
    RadioClockMinuteFrame dcf_frame;
    RadioClockMinuteFrame bpc_frame;
    RadioClockMinuteFrame hbg_frame;
    RadioClockMinuteFrame jjy_frame;
    RadioClockMinuteFrame msf_frame;
    RadioClockMinuteFrame wwvb_frame;

    radio_clock_protocol_prepare_frame(RadioClockSignalDcf77, &dcf_frame, &dcf_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalBsf, &bsf_frame, &bsf_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalBpc, &bpc_frame, &wwvb_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalHbg, &hbg_frame, &dcf_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalJjy, &jjy_frame, &wwvb_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalMsf, &msf_frame, &dcf_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalWwvb, &wwvb_frame, &wwvb_time);

    assert(memcmp(hbg_frame.encoded, dcf_frame.encoded, sizeof(hbg_frame.encoded)) == 0);

    for(uint8_t second = 0; second < RADIO_CLOCK_FRAME_SECONDS; second++) {
        if(dcf_frame.pulses[second] == RadioClockPulseMarker) {
            assert(dcf_frame.waveforms[second].segment_count == 1U);
            assert(dcf_frame.waveforms[second].segments[0].level == true);
            assert(dcf_frame.waveforms[second].segments[0].ticks == 32768U);
        } else if(dcf_frame.pulses[second] == RadioClockPulseOne) {
            assert(dcf_frame.waveforms[second].segment_count == 2U);
            assert(dcf_frame.waveforms[second].segments[0].level == false);
            assert(dcf_frame.waveforms[second].segments[0].ticks == 6554U);
            assert(dcf_frame.waveforms[second].segments[1].level == true);
            assert(dcf_frame.waveforms[second].segments[1].ticks == 26214U);
        } else {
            assert(dcf_frame.waveforms[second].segment_count == 2U);
            assert(dcf_frame.waveforms[second].segments[0].level == false);
            assert(dcf_frame.waveforms[second].segments[0].ticks == 3277U);
            assert(dcf_frame.waveforms[second].segments[1].level == true);
            assert(dcf_frame.waveforms[second].segments[1].ticks == 29491U);
        }

        if(wwvb_frame.pulses[second] == RadioClockPulseMarker) {
            assert(wwvb_frame.waveforms[second].segment_count == 2U);
            assert(wwvb_frame.waveforms[second].segments[0].level == false);
            assert(wwvb_frame.waveforms[second].segments[0].ticks == 26215U);
            assert(wwvb_frame.waveforms[second].segments[1].level == true);
            assert(wwvb_frame.waveforms[second].segments[1].ticks == 6553U);
        } else if(wwvb_frame.pulses[second] == RadioClockPulseOne) {
            assert(wwvb_frame.waveforms[second].segment_count == 2U);
            assert(wwvb_frame.waveforms[second].segments[0].level == false);
            assert(wwvb_frame.waveforms[second].segments[0].ticks == 16384U);
            assert(wwvb_frame.waveforms[second].segments[1].level == true);
            assert(wwvb_frame.waveforms[second].segments[1].ticks == 16384U);
        } else {
            assert(wwvb_frame.waveforms[second].segment_count == 2U);
            assert(wwvb_frame.waveforms[second].segments[0].level == false);
            assert(wwvb_frame.waveforms[second].segments[0].ticks == 6554U);
            assert(wwvb_frame.waveforms[second].segments[1].level == true);
            assert(wwvb_frame.waveforms[second].segments[1].ticks == 26214U);
        }

        assert(waveform_total_ticks(&bsf_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&dcf_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&bpc_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&hbg_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&jjy_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&wwvb_frame.waveforms[second]) == 32768U);
        assert(waveform_total_ticks(&msf_frame.waveforms[second]) == 32768U);
    }

    assert(jjy_frame.pulses[0] == RadioClockPulseMarker);
    assert(jjy_frame.waveforms[0].segment_count == 2U);
    assert(jjy_frame.waveforms[0].segments[0].level == true);
    assert(jjy_frame.waveforms[0].segments[0].ticks == 6553U);
    assert(jjy_frame.waveforms[0].segments[1].level == false);
    assert(jjy_frame.waveforms[0].segments[1].ticks == 26215U);

    assert(jjy_frame.pulses[1] == RadioClockPulseOne);
    assert(jjy_frame.waveforms[1].segment_count == 2U);
    assert(jjy_frame.waveforms[1].segments[0].level == true);
    assert(jjy_frame.waveforms[1].segments[0].ticks == 16384U);
    assert(jjy_frame.waveforms[1].segments[1].level == false);
    assert(jjy_frame.waveforms[1].segments[1].ticks == 16384U);

    assert(jjy_frame.pulses[3] == RadioClockPulseZero);
    assert(jjy_frame.waveforms[3].segment_count == 2U);
    assert(jjy_frame.waveforms[3].segments[0].level == true);
    assert(jjy_frame.waveforms[3].segments[0].ticks == 26214U);
    assert(jjy_frame.waveforms[3].segments[1].level == false);
    assert(jjy_frame.waveforms[3].segments[1].ticks == 6554U);

    assert(bsf_frame.pulses[39] == RadioClockPulseMarker);
    assert(bsf_frame.pulses[0] == RadioClockPulsePair00);
    assert(bsf_frame.pulses[40] == RadioClockPulsePair01);
    assert(bsf_frame.pulses[46] == RadioClockPulsePair10);
    assert(bsf_frame.pulses[47] == RadioClockPulsePair11);
    assert(bsf_frame.pulses[59] == RadioClockPulseMarker);

    assert(bsf_frame.waveforms[0].segment_count == 2U);
    assert(bsf_frame.waveforms[0].segments[0].level == false);
    assert(bsf_frame.waveforms[0].segments[0].ticks == 6554U);
    assert(bsf_frame.waveforms[0].segments[1].level == true);
    assert(bsf_frame.waveforms[0].segments[1].ticks == 26214U);

    assert(bsf_frame.waveforms[39].segment_count == 1U);
    assert(bsf_frame.waveforms[39].segments[0].level == true);
    assert(bsf_frame.waveforms[39].segments[0].ticks == 32768U);

    assert(bsf_frame.waveforms[40].segment_count == 2U);
    assert(bsf_frame.waveforms[40].segments[0].level == false);
    assert(bsf_frame.waveforms[40].segments[0].ticks == 13107U);
    assert(bsf_frame.waveforms[40].segments[1].level == true);
    assert(bsf_frame.waveforms[40].segments[1].ticks == 19661U);

    assert(bsf_frame.waveforms[46].segment_count == 2U);
    assert(bsf_frame.waveforms[46].segments[0].level == false);
    assert(bsf_frame.waveforms[46].segments[0].ticks == 26214U);
    assert(bsf_frame.waveforms[46].segments[1].level == true);
    assert(bsf_frame.waveforms[46].segments[1].ticks == 6554U);

    assert(bpc_frame.pulses[0] == RadioClockPulseMarker);
    assert(bpc_frame.pulses[1] == RadioClockPulsePair00);
    assert(bpc_frame.pulses[20] == RadioClockPulseMarker);
    assert(bpc_frame.pulses[21] == RadioClockPulsePair01);
    assert(bpc_frame.pulses[40] == RadioClockPulseMarker);
    assert(bpc_frame.pulses[41] == RadioClockPulsePair10);

    assert(bpc_frame.waveforms[0].segment_count == 1U);
    assert(bpc_frame.waveforms[0].segments[0].level == true);
    assert(bpc_frame.waveforms[0].segments[0].ticks == 32768U);

    assert(bpc_frame.waveforms[1].segment_count == 2U);
    assert(bpc_frame.waveforms[1].segments[0].level == false);
    assert(bpc_frame.waveforms[1].segments[0].ticks == 3277U);
    assert(bpc_frame.waveforms[1].segments[1].level == true);
    assert(bpc_frame.waveforms[1].segments[1].ticks == 29491U);

    assert(bpc_frame.waveforms[41].segment_count == 2U);
    assert(bpc_frame.waveforms[41].segments[0].level == false);
    assert(bpc_frame.waveforms[41].segments[0].ticks == 9830U);
    assert(bpc_frame.waveforms[41].segments[1].level == true);
    assert(bpc_frame.waveforms[41].segments[1].ticks == 22938U);

    assert(hbg_frame.waveforms[0].segment_count == 4U);
    assert(hbg_frame.waveforms[0].segments[0].level == false);
    assert(hbg_frame.waveforms[0].segments[1].level == true);
    assert(hbg_frame.waveforms[0].segments[2].level == false);
    assert(hbg_frame.waveforms[0].segments[3].level == true);

    assert(msf_frame.pulses[0] == RadioClockPulseMarker);
    assert(msf_frame.waveforms[0].segment_count == 2U);
    assert(msf_frame.waveforms[0].segments[0].level == false);
    assert(msf_frame.waveforms[0].segments[0].ticks == 16384U);
    assert(msf_frame.waveforms[0].segments[1].level == true);
    assert(msf_frame.waveforms[0].segments[1].ticks == 16384U);

    assert(msf_frame.pulses[17] == RadioClockPulseMsfA0B0);
    assert(msf_frame.waveforms[17].segment_count == 2U);
    assert(msf_frame.waveforms[17].segments[0].level == false);
    assert(msf_frame.waveforms[17].segments[0].ticks == 3277U);
    assert(msf_frame.waveforms[17].segments[1].level == true);

    assert(msf_frame.pulses[19] == RadioClockPulseMsfA1B0);
    assert(msf_frame.waveforms[19].segment_count == 2U);
    assert(msf_frame.waveforms[19].segments[0].level == false);
    assert(msf_frame.waveforms[19].segments[0].ticks == 6554U);
    assert(msf_frame.waveforms[19].segments[1].level == true);

    assert(msf_frame.pulses[54] == RadioClockPulseMsfA1B0);
    assert(msf_frame.waveforms[54].segment_count == 2U);
    assert(msf_frame.waveforms[54].segments[0].ticks == 6554U);
    assert(msf_frame.waveforms[54].segments[1].ticks == 26214U);
}

static void test_hbg_start_pulse_variants(void) {
    RadioClockProtocolTime hour_time = {
        .year = 2026,
        .day_of_year = 38,
        .month = 2,
        .day = 7,
        .weekday = 6,
        .hour = 7,
        .minute = 0,
    };
    RadioClockProtocolTime noon_time = {
        .year = 2026,
        .day_of_year = 39,
        .month = 2,
        .day = 8,
        .weekday = 0,
        .hour = 12,
        .minute = 0,
    };
    RadioClockMinuteFrame hour_frame;
    RadioClockMinuteFrame noon_frame;

    radio_clock_protocol_prepare_frame(RadioClockSignalHbg, &hour_frame, &hour_time);
    radio_clock_protocol_prepare_frame(RadioClockSignalHbg, &noon_frame, &noon_time);

    assert(hour_frame.waveforms[0].segment_count == 6U);
    assert(noon_frame.waveforms[0].segment_count == 8U);
    assert(waveform_total_ticks(&hour_frame.waveforms[0]) == 32768U);
    assert(waveform_total_ticks(&noon_frame.waveforms[0]) == 32768U);
}

static void test_following_minute_semantics(void) {
    assert(radio_clock_protocol_uses_following_minute(RadioClockSignalDcf77));
    assert(radio_clock_protocol_uses_following_minute(RadioClockSignalMsf));
    assert(radio_clock_protocol_uses_following_minute(RadioClockSignalHbg));

    assert(!radio_clock_protocol_uses_following_minute(RadioClockSignalWwvb));
    assert(!radio_clock_protocol_uses_following_minute(RadioClockSignalJjy));
    assert(!radio_clock_protocol_uses_following_minute(RadioClockSignalBpc));
    assert(!radio_clock_protocol_uses_following_minute(RadioClockSignalBsf));
}

int main(void) {
    test_dcf77_protocol_matches_known_frame();
    test_wwvb_protocol_uses_flipper_time_with_safe_aux_flags();
    test_wwvb_protocol_generates_safe_aux_frame();
    test_protocol_pulse_timing_and_phase_rules();
    test_hbg_start_pulse_variants();
    test_following_minute_semantics();
    puts("test_radio_clock_protocol: OK");
    return 0;
}
