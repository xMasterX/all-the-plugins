# Changelog

## 1.4

- Fix the SWDIO direction switch corrupting the Flipper's internal I2C bus, which could leave the
  desktop showing the error battery until the Flipper was rebooted: the raw `LL_GPIO_SetPinMode()`
  in `swd_turnaround()` is a read-modify-write of `GPIOA->MODER`, where PA9/PA10 are the fuel
  gauge, charger and LED driver bus, and it ran with interrupts enabled - a turnaround preempted
  between the load and the store wrote back a stale value and pulled those pins out from under an
  I2C transaction that was in flight

## 1.3

- Remove the call to the deprecated `view_dispatcher_enable_queue()` SDK API

## 1.2

- Fix a crash caused by log output from inside a critical section
- Fix a crash when the app was built with `DEBUG=1`

## 1.1

- Description update

## 1.0

- Initial release: standalone installer for Video Game Module firmware, either the official build
  bundled with the app or a custom `.uf2` file picked from the microSD card
