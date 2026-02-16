#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Convert hours, minutes, seconds to total seconds.
 *
 * @param h Hours (0-23)
 * @param m Minutes (0-59)
 * @param s Seconds (0-59)
 * @return Total seconds
 */
uint32_t time_helper_hms_to_seconds(uint8_t h, uint8_t m, uint8_t s);

/**
 * Convert total seconds to hours, minutes, seconds.
 *
 * @param total_seconds Total seconds to convert
 * @param h Output: hours (0-23, wraps at 24)
 * @param m Output: minutes (0-59)
 * @param s Output: seconds (0-59)
 */
void time_helper_seconds_to_hms(uint32_t total_seconds, uint8_t *h, uint8_t *m,
                                uint8_t *s);

#ifndef TIMED_REMOTE_TEST_BUILD
/**
 * Calculate seconds remaining until a target time.
 * Uses Flipper's RTC. If target time has passed, rolls to next day.
 * If target time is exactly now, returns 0.
 *
 * @param target_h Target hour (0-23)
 * @param target_m Target minute (0-59)
 * @param target_s Target second (0-59)
 * @return Seconds until target (today or next day), or 0 if now
 */
uint32_t time_helper_seconds_until(uint8_t target_h, uint8_t target_m,
                                   uint8_t target_s);

/**
 * Generate a timestamp-based signal name placeholder.
 * Format: IR_YYYYMMDD_HHMMSS
 *
 * @param buffer Output buffer (must be at least 20 bytes)
 * @param buffer_size Size of buffer
 */
void time_helper_generate_signal_name(char *buffer, size_t buffer_size);
#endif
