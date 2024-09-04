# FlipperZero_NFC_Playlist:
The idea behind this app is to allow for you to test multiple copies of NFC's at once as a bulk test
## How it works:
When starting the app you are greeted by a select file option where you choose the playlist you wanna run.

All the playlists should be placed in ext/apps_data/nfc_playlist and an example of how the data in the file should look can be found below.
```txt
/ext/nfc/link.nfc
/ext/nfc/link2.nfc
```
An example file can be found in the repository
## How to build
This app was design, built and tested using the <a href="https://github.com/Next-Flip/Momentum-Firmware">Momentum</a> so keep that in mind when building the FAP for yourself
## Supported Firmwares
As i know these firmwares are supported and working if you know any more please let me know
- <a href="https://github.com/Next-Flip/Momentum-Firmware">Momentum</a>
- <a href="https://github.com/RogueMaster/flipperzero-firmware-wPlugins">RogueMaster</a>
## Settings:
- Emulate time (How long the NFC card will be emulated for)
- Delay time (How long the gap between the cards will be)
- LED indicator (Whether or not the LED's will be on)
- Skip errors (Makes it so you can make the emulation screen hide errors and skip delays between errors and emulation)
- Reset settings (Puts all the settings back to the defaults)
## Playlist editor:
- Create PLaylist (Creates a new playlist with the given name)
- Delete playlist (Deletes the selected playlist)
- Rename playlist (Renames the selected playlist to the new name provided)
- Add NFC Item (Adds the selected nfc item to the currently selected playlist)
- Remove NFC Item (Opens a menu allowing you to select a line to remove from the playlist)
- Move NFC Item (Allows you to change the order of the NFC items in the playlist)
- View playlist content (Allows you to view the contents of the playlist)
## Feature ideas:
- A function to allow you to add multiple nfc items to a playlist at once
- A view playlist function which only shows the name of the playlist items excluding the file path

Any feedback is welcome and would be very much appreciated