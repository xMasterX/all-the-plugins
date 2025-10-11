## Version 1.9
- Use logic LOW instead of HIGH as resting state.  Fixes first LED was "laggy" issue.
## Version 1.8
- Renamed app to fit on Flipper Zero main menu screen
- Improved initialization to only happen when configuring the LEDs
## Version 1.7
- Added blanking TRESET (LOW) signal before sending data to LEDs.
- Increased timer_buffer to uint16 to support blanking signal duration.  Maybe there is a better way to do the initial low & save memory?
- Bug fix: Turn off remaining LEDs when reducing the number of LEDs.
## Version 1.6 
- Added support for up to 1000 LEDs (max set in led_driver.h)
- Added "dirty flag" to get rid of flicker when not updating the LEDs
