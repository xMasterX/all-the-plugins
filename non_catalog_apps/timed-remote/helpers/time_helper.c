#include "time_helper.h"

#ifndef TIMED_REMOTE_TEST_BUILD
#include <furi_hal.h>
#include <stdio.h>
#endif

uint32_t time_helper_hms_to_seconds(uint8_t h, uint8_t m, uint8_t s) {
  return (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
}

void time_helper_seconds_to_hms(uint32_t total_seconds, uint8_t *h, uint8_t *m,
                                uint8_t *s) {
  /* Handle times >= 24 hours by wrapping */
  total_seconds = total_seconds % (24 * 3600);

  *h = (uint8_t)(total_seconds / 3600);
  total_seconds %= 3600;
  *m = (uint8_t)(total_seconds / 60);
  *s = (uint8_t)(total_seconds % 60);
}

#ifndef TIMED_REMOTE_TEST_BUILD
uint32_t time_helper_seconds_until(uint8_t target_h, uint8_t target_m,
                                   uint8_t target_s) {
  DateTime now;
  furi_hal_rtc_get_datetime(&now);

  uint32_t now_seconds =
      time_helper_hms_to_seconds(now.hour, now.minute, now.second);
  uint32_t target_seconds =
      time_helper_hms_to_seconds(target_h, target_m, target_s);

  if (target_seconds < now_seconds) {
    /* Roll to next day */
    return (24 * 3600 - now_seconds) + target_seconds;
  }

  /* Target time is now or later today */
  return target_seconds - now_seconds;
}

void time_helper_generate_signal_name(char *buffer, size_t buffer_size) {
  DateTime now;
  furi_hal_rtc_get_datetime(&now);

  snprintf(buffer, buffer_size, "IR_%04d%02d%02d_%02d%02d%02d", now.year,
           now.month, now.day, now.hour, now.minute, now.second);
}
#endif
