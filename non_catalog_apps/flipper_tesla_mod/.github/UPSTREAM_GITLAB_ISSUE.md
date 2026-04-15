**Title:** Flipper Zero port of this project

**Body:**

Hey,

Ported the FSD unlock logic to a Flipper Zero FAP app. Supports HW3, HW4, and Legacy with auto-detection. Uses the Electronic Cats CAN Bus Add-On (MCP2515 via SPI GPIO).

Features ported from your project:
- FSD enable bits (HW3/HW4/Legacy handlers)
- Nag suppression
- Speed profile from follow-distance stalk
- Force FSD (for regions without traffic light toggle)
- ISA speed chime suppression (HW4)
- Emergency vehicle detection (HW4)
- DLC validation and setBit bounds check

Everything runs as a standalone Flipper app with a menu UI — no Arduino, no computer needed. Just plug the CAN Add-On into OBD-II and go.

Repo: https://github.com/hypery11/flipper-tesla-fsd

Already accepted into RogueMaster firmware. Catalog PR pending.

Credits to your project in our README. Licensed under GPL-3.0 to match.

Happy to coordinate if you want to link to it or if there are upstream changes we should track.

---

Open at: https://gitlab.com/Tesla-OPEN-CAN-MOD/tesla-open-can-mod/-/issues/new
