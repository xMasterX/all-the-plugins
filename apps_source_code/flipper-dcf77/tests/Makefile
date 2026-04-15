CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I../src

.PHONY: all clean test

all: test_set_dcf_message test_set_msf_message test_set_wwvb_message test_set_jjy_message test_set_bpc_message test_set_bsf_message test_hbg_util test_radio_clock_protocol test_subghz_settings test_debug_settings test_experimental_time test_tx_ratio

test: test_set_dcf_message test_set_msf_message test_set_wwvb_message test_set_jjy_message test_set_bpc_message test_set_bsf_message test_hbg_util test_radio_clock_protocol test_subghz_settings test_debug_settings test_experimental_time test_tx_ratio
	./test_set_dcf_message
	./test_set_msf_message
	./test_set_wwvb_message
	./test_set_jjy_message
	./test_set_bpc_message
	./test_set_bsf_message
	./test_hbg_util
	./test_radio_clock_protocol
	./test_subghz_settings
	./test_debug_settings
	./test_experimental_time
	./test_tx_ratio

test_set_dcf_message: test_set_dcf_message.c ../src/dcf77_util.c ../src/dcf77_util.h
	$(CC) $(CFLAGS) -o $@ test_set_dcf_message.c ../src/dcf77_util.c

test_set_msf_message: test_set_msf_message.c ../src/msf_util.c ../src/msf_util.h
	$(CC) $(CFLAGS) -o $@ test_set_msf_message.c ../src/msf_util.c

test_set_wwvb_message: test_set_wwvb_message.c ../src/wwvb_util.c ../src/wwvb_util.h ../src/radio_clock_pulse.h
	$(CC) $(CFLAGS) -o $@ test_set_wwvb_message.c ../src/wwvb_util.c

test_set_jjy_message: test_set_jjy_message.c ../src/jjy_util.c ../src/jjy_util.h ../src/radio_clock_pulse.h
	$(CC) $(CFLAGS) -o $@ test_set_jjy_message.c ../src/jjy_util.c

test_set_bpc_message: test_set_bpc_message.c ../src/bpc_util.c ../src/bpc_util.h ../src/pair_timecode.c ../src/pair_timecode.h ../src/radio_clock_pulse.h
	$(CC) $(CFLAGS) -o $@ test_set_bpc_message.c ../src/bpc_util.c ../src/pair_timecode.c

test_set_bsf_message: test_set_bsf_message.c ../src/bsf_util.c ../src/bsf_util.h ../src/pair_timecode.c ../src/pair_timecode.h ../src/radio_clock_pulse.h
	$(CC) $(CFLAGS) -o $@ test_set_bsf_message.c ../src/bsf_util.c ../src/pair_timecode.c

test_hbg_util: test_hbg_util.c ../src/hbg_util.c ../src/hbg_util.h ../src/radio_clock_protocol.h ../src/radio_clock_pulse.h ../src/radio_clock.h
	$(CC) $(CFLAGS) -o $@ test_hbg_util.c ../src/hbg_util.c

test_radio_clock_protocol: test_radio_clock_protocol.c ../src/radio_clock_protocol.c ../src/radio_clock_protocol.h ../src/bsf_util.c ../src/bsf_util.h ../src/bpc_util.c ../src/bpc_util.h ../src/pair_timecode.c ../src/pair_timecode.h ../src/dcf77_util.c ../src/dcf77_util.h ../src/hbg_util.c ../src/hbg_util.h ../src/jjy_util.c ../src/jjy_util.h ../src/msf_util.c ../src/msf_util.h ../src/wwvb_util.c ../src/wwvb_util.h ../src/radio_clock.h ../src/radio_clock_pulse.h
	$(CC) $(CFLAGS) -o $@ test_radio_clock_protocol.c ../src/radio_clock_protocol.c ../src/bsf_util.c ../src/bpc_util.c ../src/pair_timecode.c ../src/dcf77_util.c ../src/hbg_util.c ../src/jjy_util.c ../src/msf_util.c ../src/wwvb_util.c

test_subghz_settings: test_subghz_settings.c ../src/subghz_settings.c ../src/subghz_settings.h
	$(CC) $(CFLAGS) -o $@ test_subghz_settings.c ../src/subghz_settings.c

test_debug_settings: test_debug_settings.c ../src/debug_settings.c ../src/debug_settings.h
	$(CC) $(CFLAGS) -o $@ test_debug_settings.c ../src/debug_settings.c

test_experimental_time: test_experimental_time.c datetime_stub.c datetime_stub.h ../src/experimental_time.c ../src/experimental_time.h
	$(CC) $(CFLAGS) -DDCF77_HOST_TEST -I. -o $@ test_experimental_time.c datetime_stub.c ../src/experimental_time.c

test_tx_ratio: test_tx_ratio.c ../src/tx_ratio.c ../src/tx_ratio.h
	$(CC) $(CFLAGS) -o $@ test_tx_ratio.c ../src/tx_ratio.c

clean:
	rm -f test_set_dcf_message test_set_msf_message test_set_wwvb_message test_set_jjy_message test_set_bpc_message test_set_bsf_message test_hbg_util test_radio_clock_protocol test_subghz_settings test_debug_settings test_experimental_time test_tx_ratio
