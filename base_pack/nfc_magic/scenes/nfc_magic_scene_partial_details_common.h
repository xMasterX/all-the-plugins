#pragma once

#include <furi.h>
#include <stdint.h>

// Append the indices of the set bits in `bitmap` (scanned over [0, count)) to `out`, ascending, as
// space-separated decimals -- e.g. "5 6 7 ". These bitmaps mark which block/page failed to write, and
// this list is shown on the Gen2, USCUID-UL and ISO15693 partial-result screens (previously each
// re-implemented the same loop). If `max_shown` > 0 and that many indices have been appended, append
// "..." and stop; pass 0 for no cap. Bit N == index N.
void nfc_magic_partial_details_append_indices(
    FuriString* out,
    const uint8_t* bitmap,
    uint16_t count,
    uint16_t max_shown);
