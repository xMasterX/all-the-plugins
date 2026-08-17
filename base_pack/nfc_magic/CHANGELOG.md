# Changelog

## 2.1

Adds magic **ISO15693 / NfcV** support. Detect an ISO15693 tag, show its Info, and
**clone / wipe** a magic ISO15693 card the same way the app handles its other magic types.

The magic write frames follow proxmark3's `SetTag15693Uid` (gen1) and `SetTag15693Uid_v2` (gen2):
gen1 is a verbatim port, and so is gen2's command structure — except that in clone mode the gen2 CFG
frame substitutes the source card's geometry and IC ref (in place of proxmark's fixed `3f 03 8b`), so
the copy advertises the same chip identity.

### Added
- **Detection** — any ISO15693 tag that activates is treated as a magic candidate and routed to a
  dedicated menu (Write / Wipe / Write UID / Info), mirroring the other magic types.
- **Info** — UID, manufacturer, chip type, GET SYSTEM INFO (memory / DSFID / AFI / IC ref), and the
  full block data (scrollable, `*` marks a locked block). Chip decode tells NXP **SLI / SLIX / SLIX2**
  (and the -S / -L variants) apart via the UID type-indicator bits.
- **Clone from a saved `.nfc`** — writes the UID (magic backdoor), all data blocks, and the source's
  identity (IC ref / block geometry / AFI / DSFID) so the copy advertises the same chip. gen2 sets
  UID + geometry via the `0xE0` magic command; if the card turns out not to be gen2, gen1 is offered
  as an explicit opt-in (see below).
- **Wipe** — zero every data block the card physically holds, including 56/57/62/63. On a gen2 card
  those are ordinary user data, so sparing them would leave real data behind on the card people
  actually have. On a **gen1** card they are the UID / unlock / commit registers, so a wipe cannot
  promise to leave the UID intact — instead it **re-reads the UID afterwards and reports a change**
  rather than claiming one (see below). Like proxmark's `hf 15 wipe`, no attempt is made to disarm the
  card first; whether that is needed is flagged in the code as an open question pending a gen1 card to
  test against.
- **The wipe is bounded by the card, not by what the card claims.** A magic card's advertised block
  count is programmable — cloning a 28-block source onto a 64-block card makes it advertise 28 — while
  the blocks above that count stay readable and writable. A wipe that trusted the count would therefore
  clear 28 of 64 and report success, leaving the previous card's data reachable. Instead the wipe sweeps
  upward past the advertised count until a run of blocks answers neither a write nor a read, and never
  stops early on a dead stretch inside the range the card claims. Two limits do end it: the 256-block
  ceiling, and a time limit for a card that answers reads at every address and so never accumulates a
  run. A wipe stopped by that limit is reported as **partial** — it names where it stopped and offers a
  retry, because blocks above the cut may still hold data.
- **A wipe reports the range it covered** — "Cleared *N* blocks. Card claims *M*." Both figures, no
  verdict where the difference is benign: the advertised count is programmable, so a card cloned from a
  smaller source, or one with fake flash, will show a mismatch without anything being wrong. Where the
  difference *is* a fault — a dead stretch inside the claimed range, or a sweep the clock cut short —
  it is reported as such rather than hidden in the counts.
- **Blocks the card claims are not written off without evidence.** A block that answers neither a write
  nor a read may be memory that does not exist, or memory that has stopped responding while still
  holding data. Below the card's own claimed count the wipe distinguishes them: a block whose contents
  were read when the card was first activated provably exists and provably held data, so it is reported
  as uncleared rather than dropped as absent.
- **Live "Writing X / N" progress** during a clone or wipe, as the USCUID-UL clone already had.
- **Write UID** — manual magic backdoor UID write. Tries gen2 first and, only if that leaves the UID
  unchanged, offers the same opt-in gen1 attempt the clone does.

### Behaviour
- **The clone attempts every source block and reports only real data loss.** WRITE BLOCK on these cards
  is gated by physical memory rather than by the advertised block count, so every block is attempted. A
  non-empty block that won't write is reported as **Partial**, naming the blocks. An empty block past
  the card's real capacity loses nothing, so the clone is a **Success** carrying a note that the card
  advertises more blocks than it physically holds — reading one of those blocks gives an error rather
  than the zeros the source had there, and real data can't be stored in them. A card whose advertised
  geometry exceeds its physical memory (fake-flash) clones faithfully for the blocks that fit.
- **No data is written until the card takes the magic UID.** The write sends the gen2 backdoor UID
  first; data blocks and identity fields follow once that UID reads back as the target. A card that
  doesn't take it is left untouched, so cloning has no up-front confirmation prompt — matching Gen2 and
  Classic, which also write without one when their pre-write checks find nothing to report. The
  destructive gen1 attempt carries its own consent screen instead. A wipe does prompt, since destruction
  is a wipe's only product, whereas a clone leaves the card holding the image the user picked.
- **The gen1 fallback is opt-in.** It is offered only when the gen2 write leaves the UID unchanged, and
  only after the user accepts a screen stating what gen1 writes and that the gen1 path is not
  hardware-tested. gen1 writes the UID registers first and the data blocks only if that UID took, so a
  tag that turns out not to be gen1 loses at most those four blocks. Write UID uses the same flow.
- **gen1 fidelity is surfaced.** The gen1 backdoor stores the UID in data blocks 56/57 plus
  unlock/commit in 62/63, so a gen1 clone can't reproduce a source that keeps data there. The opt-in
  screen says so before anything is written, and a clone that used gen1 reports Partial and flags those
  blocks.
- **Writes are verified by read-back.** The UID is re-read after an RF field power-cycle
  (`NfcCommandReset`), so a card that only latches a new UID after a reset still verifies. The source's
  AFI / DSFID are re-read with GET SYSTEM INFO and compared; a field the copy doesn't carry is reported
  as Partial with a note. Block contents are not compared — a data block counts as written when the
  card acknowledges it.
- **A wipe counts a block as unwiped unless it can show the block is clear**, by reading it back after a
  failed write. A block that answers a read but still holds data is a real failure and is named. A block
  that answers nothing is provisional: if anything above it answers, it is an interior fault and counted;
  if the run continues to the end of the sweep it is treated as the space above the card's real top —
  but only above the count the card claims, and only where the block was not read at activation. Before
  concluding any of it, the run is re-probed, so a momentary dropout is not mistaken for the end of the
  card.
- **A wipe re-reads the UID when it finishes**, behind an RF field power-cycle, since a gen1 card
  latches a written UID only on the next power-up. The wipe sends no UID command, but on a gen1 card
  blocks 56/57 *are* the UID registers. If the UID read back differs from the one the card presented, the result
  is reported as partial on a **"UID changed"** screen that prints the UID the card now answers to —
  without which the card would be unreachable. If the card never comes back from the power-cycle, or no
  longer answers at all, the check has reached no answer: the wipe says "UID not re-checked" rather than
  implying the identity was confirmed.
- **A card lifted mid-write reports "Card removed".** Losing the card partway through makes every
  remaining block fail, which looks the same as reaching the card's physical capacity, so when a block
  fails the write re-checks that the card is still present before reporting a capacity verdict. Both the
  wipe and the clone's data pass are bounded by a time limit as well, so a card that leaves mid-write is
  reported rather than leaving the screen held for the length of the whole block range.
- **Partial and over-capacity results are a summary plus a Details screen**, matching the Gen2 /
  USCUID-UL partial screens. The summary carries the counts and the most significant caveat, and
  **Details** lists the blocks involved plus any further caveats — the gen1 56/57/62/63 overwrite, or
  an AFI/DSFID the card wouldn't take. A clean clone is a plain success screen, a clean wipe reports its
  block range, and the outright failures are a single message.
- Each outcome has its own screen: **"Card removed"**, **"Nothing to clone"** for a source with no data
  blocks, a wipe failure saying no block accepted the zero-write, and a clone failure
  for the case where the UID was written but not one data block would take — the card would otherwise
  look right to a UID-only reader while holding none of the data. Detect and write popups time out
  after a few seconds with no card.
- **A UID that moves somewhere unasked-for is reported as magic, not as a dud.** If the gen2 backdoor
  changes the UID to neither the original nor the one requested, that is the one result that *proves*
  the card is magic — an inert tag can't change its UID — so the screen says so and prints the UID the
  card now answers to, rather than reporting "not a magic tag".
- **A failed gen1 attempt names the blocks it spent.** The gen1 UID sequence goes out as four ordinary
  writes before anything can be verified, so if the UID doesn't take, blocks 56/57/62/63 have already
  been overwritten on what is most likely an ordinary tag. The failure screen says which blocks, so
  they can be restored from a backup.
- **Back is ignored during an ISO15693 write**, from the moment a card is found until the write reports
  an outcome. It cannot abort a write in any case — leaving the screen waits for the write to finish and
  then discards its report — and on a clone, pressing it between the UID write and the data pass could
  leave the card carrying a new UID with none of the source's data. Other magic protocols are unchanged.
- **Write UID refuses to "verify" a UID the card already has.** The editor pre-fills with the UID from
  the last Info read, so writing it straight back is two taps away — and a read-back against a UID the
  card already carries is passed by any tag at all, magic or not. That would have reported Success
  having proved nothing, so the write is refused up front with an explanation instead.

### Validation (at 2.1)
- The **gen2** path was validated end-to-end on hardware for this release: byte-identical clones
  across 28 / 56 / 64 / 70-block geometries, plus wipe and the over-capacity reporting.
- The **gen1** path shipped as a faithful proxmark port, not tested against gen1 hardware (none was
  available).
- **gen3 is not supported.** A third magic generation exists — proxmark's `hf 15 csetuid --v3` — which
  keeps its UID in blocks 0x10/0x11 with a configuration signature in 0x14/0x15, and is rewritable until
  `hf 15 cfinalize` locks it. Such a card reports "not a magic tag" here, and a wipe or clone writes over
  those blocks like any other data.

## 2.0

Major release. Adds magic **Ultralight / NTAG (USCUID-UL)** support, and reworks the magic
**MIFARE Classic (Gen2)** wipe & clone with honest **Success / Partial / Fail** reporting.

### Added

- **Magic Ultralight / NTAG (USCUID-UL) support** — the app can now write, clone and wipe magic
  Ultralight-family tags, not just MIFARE Classic:
  - **Write / full clone** with the transport auto-selected from detection — **direct** (CUID/ATS,
    ISO14443-3 + `A2`) or **backdoor** (raw wakeup). ACK-only, continue-on-fail; the UID page is
    written last; live "Writing X/N" progress; **Partial Write** with a Details list of the pages
    that didn't take (and resume if the card is briefly removed mid-write).
  - **Wipe** to factory default.
  - **Auth with Password (PWD-AUTH)** to write protected/locked tags (single attempt, so it can't
    burn `AUTHLIM`).
  - **"Write anyway"** for tags that don't confirm as magic.
  - **Detection** of UL11 / UL21 (incl. Mikron "Ultra"), NTAG213/215/216, UL-C, UL-5, and
    backdoor-mode tags (both wakeup sequences), with a raw-config view for unrecognised presets;
    family-first scanner.
- **Gen2 / MFC wipe & clone now report Success / Partial / Fail** — a **Partial** screen plus a
  **Details** list of the exact blocks that couldn't be written/wiped (it was previously always
  "Success").
- **"No keys found"** screen when a wipe has no usable keys (instead of a doomed write-check).
- **Exit → menu** button on the Wipe / Write / Dump failure screens.

### Changed

- Gen2 / MFC wipe & clone outcomes are tracked **per block** (counts + the not-done block list).
- A wiped **block 0** preserves the card's real **SAK/ATQA** (a 1K mis-detected as 4K is no longer
  stamped 4K) and zeroes the UID.
- **Write-check warnings** reworked: one shared Gen2/Classic handler and consistent
  **Back / Next / Skip** buttons.
- Clearer rejection message for a cross-family wrong dump.

### Fixed

- **False "Success"** on a Gen2/MFC **wipe** with missing keys, and on a **clone** that could write
  nothing (unknown keys, or a read-only block 0).
- **Stale-key carry-over** between operations — the dictionary attack now re-reads the card fresh
  each time instead of replaying the previous tag's keys.
- **Per-sector → per-block** failure tracking — a read-only block 0 no longer reports the whole of
  sector 0 as failed.
- Dead **"Next"** button on a write-check warning whose first item wasn't the UID warning.
- Gen2 write-check **"Back"** did nothing (it targeted the wrong menu); the misleading **"Retry"**
  label → **"Back"** (the button always navigates back).
- **KeysDict** memory leak when backing out of a write-check.
- The wipe-failure screen could show **"No keys found"** for an unrelated failure (uninitialised
  scene state).

## 1.12

### Added

- **Gen2 CUID / ATS + static-nonce detection** — classify magic MIFARE Classic sub-types (direct
  CUID, ATS-fingerprinted, and CUID with a static nonce).
- **Gen1 4- and 7-byte UID** handling, including writing a 7-byte MIFARE Classic dump to a Gen1 tag.
- **Length-aware wipe & write guards** — validate block counts before writing.

### Changed

- Updated for new firmware API / SDK.

### Fixed

- Gen4 max block number for the NTAG protocol.

## 1.11

### Changed

- Description / metadata update.

### Fixed

- Gen4 poller fix.

## 1.10

- Upstream sync and maintenance.

## 1.9

### Changed

- Gen4 sync.

### Fixed

- Minor UI fix.

## 1.8

### Added

- **Gen4 (UMC / GTU) magic-card support.**

## 1.7

### Changed

- Reverted not-release-ready Gen2 changes (Gen4 still pending).

### Fixed

- UI and newline fixes.

## 1.6

### Changed

- Incremental updates; Gen2 preparation added, then reverted as not release-ready.

## 1.5

### Changed

- Reworked Back-button event handling; GUI cleanup.

### Fixed

- Incorrect total-block usage (#102); ufbt build compatibility.

## 1.4

- Maintenance / upstream sync.

## 1.3

### Fixed

- New-API compatibility fixes.

## 1.1

- Early release updates; new-API compatibility pass.

## 1.0

### Added

- Initial release in the plugin pack: write & wipe magic cards (**Gen1a**, **Gen2**) with
  block-0 / UID editing.
