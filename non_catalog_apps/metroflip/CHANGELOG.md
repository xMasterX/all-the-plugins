## v1.0
- Suica Fixes
- New Cards:
- Intertic disposable ST25TB cards adding 21 French cities and companies
- T-Mobilitat can parse card number from historical bytes
- TRT
- Fix a lot of bugs relating mifare classic and stuff

## v0.9

- Fix unsupported card crash
- RENFE Suma 10 support ADDED 
- GEG Connect AID added to DESfire list.
- Top Up log parsing and animations.
- 16 new rail lines, including JR lines like Chuo, Negishi, Joban, and Yamanote; and all of Tokyu's operating lines.
- Added support for parsing area codes for future expansion on non-Tokyo areas.
- Added saving function for Suica/Japan Rail IC readings.
- Various bug fixes and safer memory manegements improvements.

## v0.8

- Added 80+ card AIDs (most may not be fully parsed)
- Added more AIDs for DESFire
- Added Calypso card saving support
- Fixed DESFire parsing
- Fixed Navigo crash
- Fixed crash when opening Navigo files after exit
- Fixed Clipper timestamp epoch conversion
- Fixed Calypso file saving 

## v0.7

- Fixed the stuck-in-app loop  
- Added balance to Rav-Kav  
- Added Suica parser  
- Stylization updates  

## v0.6

- Added a load mode and a save mode to store card info  
- Fixed a major bug due to API symbol not existing  

## v0.5

Big update!

- Custom API Added: A custom API for Metroflip has been introduced for smoother operation and better scalability  
- Parsers Moved to Plugins: All parsers have been moved to individual plugins, loaded from the SD card as '.fal' files  
- Scene Optimization: All scenes merged into 'metroflip_scene_parse.c' for simplification  
- RAM Usage Reduced: Over 45% reduction in RAM usage  
- Navigo Station List: Moved to 'apps_assets'
- Unified Calypso Parser: Thanks to DocSystem  
- Rav-Kav Moved to Calypso Parser: Credit to luu176  

## v0.4

- Updated Navigo parser (thanks to DocSystem)  
  - Now uses a global Calypso parser with defined structures  
  - Fixes BusFault and NULL pointer dereferences  
- Updated all Desfire parsers (Opal, ITSO, Myki, etc.)  
  - Fixed crash when pressing back button while reading  
- Fix Charliecard parser  

## v0.3

- Added Clipper parser (San Francisco, CA, USA)  
- Added Troika parser (Moscow, Russia)  
- Added Myki parser (Melbourne, VIC, Australia)  
- Added Opal parser (Sydney, NSW, Australia)  
- Added ITSO parser (United Kingdom)  

## v0.2

- Updated Rav-Kav parsing to show more data like transaction logs  
- Added Navigo parser (Paris, France)  
- Bug fixes  

## v0.1

- Initial release by luu176
