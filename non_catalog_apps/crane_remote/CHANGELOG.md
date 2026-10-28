# Changelog

Telecrane Control — forge frames for Telecrane-style 2FSK crane remotes.
Original app by [nmatt0](https://github.com/nmatt0/telecrane-contol).

## 1.1 — extra pack

Changes made for this pack:

- External CC1101 module support, auto-detected; the sniff screen shows which
  radio the sweep is running on
- Fits in memory: the sniff buffers were 80K of static data, which stopped the
  app loading at all and starved the radio of its DMA buffers
- Serial entry validates on the keyboard, with the format shown, and comes
  prefilled — an invalid serial used to silently discard what you typed
- "My RCs" shows `(no remotes saved)` instead of an empty screen
- Fixed: a 17th remote wrote past the end of the list
- Fixed: an unparsable serial put an uninitialised frequency on the air
- Fixed: a region struct was leaked on every transmit and every sniff sweep
- Saved remotes are validated on load, so a damaged config cannot reach the
  transmitter
- Debug logging to the SD card is off by default (`CRANE_DEBUG_LOG`)

## 1.0 — upstream

- Initial release by nmatt0: TX by button mask, saved remotes, channel sniffer
