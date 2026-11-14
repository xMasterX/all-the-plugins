# T5577 MultiWriter

Store up to three EM41xx UIDs on a single T5577, and check that a reader can actually see them.

PoC of https://forum.dangerousthings.com/t/hack-store-2-to-3-different-em41xx-on-a-single-t5577/12116

## How it works

A T5577 broadcasts its data blocks one after another, as many as block 0's `maxblock` field says, and repeats. An EM4100 frame is 64 bits, so it fits in two blocks. Blocks 1 to 6 therefore hold three keys, and block 0 decides how many of them go out. Block 7 is the password block, which is why only three keys fit.

A reader has nothing to tell it where one key ends and the next begins, so it takes whichever EM4100 frame it happens to decode. That is what makes the tag work at all, and also why reading it once proves nothing - hence **Test tag**, which counts fifty frames and reports what came back.

It also explains which readers accept these tags. A reader that requires several consecutive frames to agree before accepting a read can never accept one, since no two consecutive frames carry the same key. The Flipper's own LF-RFID worker is such a reader - it wants `validate_count` identical decodes in a row - so the stock RFID app cannot read a tag this app writes. Test tag therefore runs its own EM4100 listener, counting every frame the decoder yields.

## Menu

| Entry | What it does |
| --- | --- |
| Check tag | Reads the tag and lists the keys it holds, reporting what it broadcasts and what it holds as separate counts, and marking the rows where the two disagree |
| Write first/second/third key | Writes a saved `.rfid` dump into that slot, then reads every written block back |
| Test tag | Counts which key an ordinary EM4100 reader gets, over fifty frames |
| Password | Locks the tag against writes, unlocks it, or forgets the stored word |

## Requirements

Unleashed firmware with API 88.22 or later, for the T5577 block-read API (`t5577_detect`, `t5577_block_value`).
