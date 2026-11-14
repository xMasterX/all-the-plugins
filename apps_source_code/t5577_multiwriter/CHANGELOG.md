# Changelog

## 0.3

- Read every written block back off the tag and report what it actually holds. A write the chip refused, or a tag lifted part way through, was previously reported as a success - T5577 writes carry no acknowledgement, so reading the blocks back is the only evidence there is
- Add Check tag: read a tag and list the keys it holds, with the EM4100 ID of each. Reports what block 0 broadcasts and what the slots were found to hold as two separate counts, because the two disagree in both directions: a tag rewritten for fewer keys stores keys it no longer sends, and a tag written out of order sends slots that hold no key at all. Each row says which of the two it is
- Add Test tag: count which key an ordinary EM4100 reader gets, over fifty frames. A key that never turns up is a key no reader will see
- Add Password: lock a tag against writes and unlock it again. The password block is written and read back before protection is turned on, so a tag is never left asking for a word it does not hold
- Keep a tag's password for the session once the tag has answered to it, so writing another key to the same tag needs no retyping, and forget it from the same menu entry
- Warn before a write that would hide keys already on the tag, or that leaves earlier slots empty
- Show a progress bar through a write, a check and a lock, each of which takes several seconds of reading
- Read the tag with the app's own EM4100 listener rather than the firmware's RFID worker. The worker reports a read only after three consecutive frames decode to the same key, and a multi-key tag never sends the same key twice running, so it cannot read these tags at all - which is also why a strict reader rejects them
- Carry captured periods from the interrupt to the decoder over a lock-free ring rather than a stream buffer, which cannot be fed from an interrupt in this firmware, and allocate it only while a test is running
- Require Unleashed firmware with API 88.22 or later, for the T5577 block-read API
- Raise the stack to 4 KB, for the block reads this now does
- Show the app as T5577 MultiWriter in the menu

## 0.2

- Initial release: write up to three saved EM4100 dumps to one T5577 blank
