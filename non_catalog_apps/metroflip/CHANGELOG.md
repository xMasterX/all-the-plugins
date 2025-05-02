## v0.1

- Initial release by luu176

## v0.2

- Update Rav-Kav parsing to show more data such as transaction logs
- Add Navigo parser! (Paris, France)
- Bug fixes

## v0.3

- Added Clipper parser (San Francisco, CA, USA)
- Added Troika parser (Moscow, Russia)
- Added Myki parser (Melbourne (and surrounds), VIC, Australia)
- Added Opal parser (Sydney (and surrounds), NSW, Australia)
- Added ITSO parser (United Kingdom)

## v0.4

- Updated Navigo parser (Paris, France) (thanks to: DocSystem)
  - Now use a global Calypso parser, with defined structures
  - Fix Busfault and NULL Pointer Dereferences
- Updated all Desfire parsers (opal, itso, myki, etc..)
  - Now doesnt crash when you click the back button while reading
- Fix Charliecard parser

## v0.5

Big update!

- Custom API Added: A custom API for Metroflip has been introduced for smoother operation and better scalability.
- Parsers Moved to Plugins: All parsers have been moved to individual plugins, which means all parser-related code is now loaded from the SD card as .fal files, freeing up RAM until a parser is needed.
- Scene Optimization: All separate scenes have been merged into one unified scene (metroflip_scene_parse.c), simplifying the codebase.
- RAM Usage Reduced: We’ve reduced RAM usage by over 45%, eliminating those frustrating "out of memory" errors.
- Navigo Station List: The Navigo station list has been relocated to apps_assets, improving organization and performance.
- Unified Calypso Parser: A new unified Calypso parser has been introduced (thanks to DocSystem), streamlining Calypso card support.
- RavKav Moved to Calypso Parser: RavKav has been moved to the new unified Calypso parser (credit to luu176).

## v0.6

- Added a load mode and a save mode to store card info
- Fixed a major bug due to API symbol not existing

## v0.7

- Fixed the stuck in app loop.
- Added balance to RavKav.
- Added Suica parser
- Stylization updates


