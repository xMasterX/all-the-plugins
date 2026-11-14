# SubGHz Scheduler

A Flipper Zero app to send SubGHz signals at a given interval. Individual `*.sub` or playlist `*.txt` files can be used for transmission, and schedule settings can be saved in a `*.sch` file.

Playlists can also be stepped through one line at a time, with a separate wait or a time of day (HH:MM:SS) per line. See [Playlist Scheduling](#playlist-scheduling).


<table style="border:0px">
  <tr style="border:0px">
    <td style="border:0px" width="25%" align="left">
    Schedule intervals in H:M:S format <br>
    from 00:00:01 to 99:59:59.<br><br>
    Left/Right select hours, minutes or seconds, Up/Down change the value.
    OK saves, Back cancels.
    </td>
    <td style="border:0px" width="20%"><img src="./screenshots/v3/intervals.png" width="100%"></td>
  </tr>
  <tr style="border:0px">
    <td style="border:0px" width="25%" align="left">
    Interval timing modes: <br>
     <ul>
       <li>Relative: Interval is length of time from `END` of one transmission to `START` of next.</li>
       <li>Precise: Interval is length of time from `START` of one transmission to `START` of next.</li>
     </ul>
     Not used with the <code>Time</code> schedule.
    </td>
    <td style="border:0px" width="20%"><img src="./screenshots/v3/timing.png" width="100%"></td>
  </tr>
 <tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
    Data Tx Count:<br>
  <ul>
  <li>x1, x2, x3, x4, x5, x6</li>
  </ul>
  </td>
  <td style="border:0px" width="20%"><img src="./screenshots/v3/txcount.png" width="100%"></td>
<tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
    Transmission Modes:<br>
  <ul>
  <li>Normal: Transmit after intervals.</li>
  <li>Immediate: Counts start as interval.</li>
  <li>One-Shot: Wait interval and transmits once. With a stepped playlist, sends one line (plus any following lines with a zero wait).</li>
  </ul>
  </td>
  <td style="border:0px" width="20%"><img src="./screenshots/v3/txmode.png" width="100%"></td>
 </tr>
<tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
   Inter-transmission delay selectable<br>
   from 0ms to 1000ms in 50ms increments.
  </td>
   <td style="border:0px" width="20%"><img src="./screenshots/v3/txdelay.png" width="100%"></td>
  </tr>
  <tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
   Radio Selection:<br>
   <ul>
    <li>Internal</li>
    <li>External (When Present)</li>
   </ul>
  </td>
   <td style="border:0px" width="20%"><img src="./screenshots/v3/radio.png" width="100%"></td>
  </tr>
    <tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
   Save settings for later use.<br>
  </td>
   <td style="border:0px" width="20%"><img src="./screenshots/v3/saveschedule.png" width="100%"></td>
  </tr>
  <tr style="border:0px">
  <td style="border:0px" width="25%" align="left">
   Run view displays:
    <ul>
      <li>TX Mode</li>
      <li>TX Interval</li>
      <li>TX Count</li>
      <li>Radio</li>
      <li>Selected File</li>
      <li>TX Interval Countdown (<code>Sending</code> while transmitting)</li>
      <li>Next playlist line, when stepping</li>
    </ul>
  </td>
   <td style="border:0px" width="20%"><img src="./screenshots/v3/rundisplay.png" width="100%"></td>
  </tr>
</table>

## Playlist Scheduling

When a playlist (`*.txt`) is selected, extra options appear in the schedule menu.

**Step**
* `All`: every trigger sends the whole playlist (default, same as before).
* `One`: every trigger sends the next line of the playlist. After the last line it starts over at line 1.

**Schedule** (only with Step `One`)
* `Uniform`: one interval for every line, set with `Interval`.
* `Per Line`: every line has its own wait before it is sent. A wait of `00:00:00` sends the line right after the previous one, so lines can be grouped.
* `Time`: every line is sent at a time of day (`HH:MM:SS`), in playlist order, repeating daily. Lines with the same time are sent one after the other.

**Start** (only with Schedule `Time`)
* `Line 1`: start with line 1 and wait for its time, even if that is tomorrow (default).
* `Nearest`: start with the line whose time comes up first, then continue in playlist order. Started at 19:00 in the example below, line 4 is sent at 23:00 and line 1 the next day at 17:30:00.

Set the wait or time of each line under `Edit Lines...`. The same time editor is used: Left/Right select a block, Up/Down change it, OK saves and Back cancels. These settings are stored in the `*.sch` file; the playlist itself is not changed and still works with the stock `SubGHz Playlist` app.

Example: a playlist with 4 lines and Schedule `Time`:

| Line | Time | Sent |
|---|---|---|
| 1 | 17:30:00 | 17:30:00 |
| 2 | 18:00:00 | 18:00:00 |
| 3 | 18:00:00 | right after line 2 |
| 4 | 23:00:00 | 23:00:00, then line 1 again the next day at 17:30:00 |

Notes:
* To send the same signal more than once a day, list its `*.sub` file more than once in the playlist. Every entry is a separate line with its own wait or time.
* Times use the Flipper clock, so make sure date and time are set correctly.
* Playlists can have up to 127 lines.
* The red LED only blinks during the last minute before a send, so long waits stay dark.
* The app has to stay open on the run screen. Charging is only blocked while transmitting, so the Flipper can stay on USB power for long schedules.


## Requirements

This app is tested against the current `dev` and `release` branches of the [OFW](https://github.com/flipperdevices/flipperzero-firmware):

* Current OFW Version: 1.5.1
<br>[![Compatibility status:](https://github.com/shalebridge/flipper-subghz-scheduler/actions/workflows/build.yml/badge.svg)](https://github.com/shalebridge/flipper-subghz-scheduler/actions/workflows/build.yml)

It has also been tested against [Momentum 009](https://github.com/Next-Flip/Momentum-Firmware/releases/tag/mntm-009). Playlist scheduling (v3.2) was tested on Momentum (API 87.1).

> [!NOTE]
> Currently not supported on Xtreme, as development of that firmware was officially [discontinued on Nov 18, 2024](https://github.com/Flipper-XFW/Xtreme-Firmware/commit/54619d013a120897eeade491decf4d1e95217c06). Since then, breaking changes were made to the Furi API. For those who still use Xtreme I am working on a separate port.

## Build

These apps are built using [ufbt](https://pypi.org/project/ufbt/) - a subset of the flipper build tool (fbt) targeted at building apps. Install it with:

```bash
pip install ufbt
```

For build only, run `ufbt` from the terminal in the project directory. To upload, make sure Flipper is connected to your computer and run `ufbt launch`.

The directory contains the following batch script(s) to simplify the upload process:
* `launch_win.bat` - Windows only. Invokes `ufbt` to deploy and launch the app on a flipper over USB, and resets terminal colors in case of error.

Build outputs are found in the `dist` directory for each application.


# To-Do
- [ ] Enable running in the background.
- [x] Enable interval delays for playlists. For example, turning on a light (playlist index 1), then 4 hours later turning it off (playlist index 2). See Step `One` with Schedule `Per Line`.
- [ ] More visual feedback of current transmission (like `SubGHz Playlist`).
- [ ] Options to select notifications on transmit (vibro, backlight, etc).
