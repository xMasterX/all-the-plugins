# FlipperZero_NFC_Comparator
This is a simple tool for checking NFC cards using a Flipper Zero device. It allows you to compare a stored NFC card against a physical card, checking for UID, UID length, and protocol. The tool is designed to help users identify and manage NFC cards, especially those with no distinguishing characteristics. The comparator can also compare two digital NFC cards, checking for UID, UID length, protocol, and NFC data.

This tool also offers a finder feature that searches for matches between physical NFC cards and stored digital NFC cards, making it easier to locate specific cards in your collection. This can also be used to locate duplicate digital NFC cards that you have saved on your Flipper Zero.
## Feedback:
Any feedback is welcome and would be very much appreciated as it helps me to improve and build upon the project
- <a href="https://github.com/acegoal07/FlipperZero_NFC_Comparator/issues/new?assignees=acegoal07&labels=enhancement&projects=&template=feature_request.md&title=%5BFEATURE%7D">Feature request</a>
- <a href="https://github.com/acegoal07/FlipperZero_NFC_Comparator/issues/new?assignees=acegoal07&labels=bug&projects=&template=bug_report.md&title=%5BBUG%5D">Bug report</a>
## Supported Firmwares:
As i know these firmwares are supported and working if you know any more please let me know
- <a href="https://github.com/Next-Flip/Momentum-Firmware" target="_blank">Momentum</a>
- <a href="https://github.com/RogueMaster/flipperzero-firmware-wPlugins" target="_blank">RogueMaster</a>
- <a href="https://github.com/DarkFlippers/unleashed-firmware" target="_blank">Unleashed</a>
## Features:
- Compare Menu:
  - Compare a digital NFC card against a physical card checking for:
  >  - UID
  >  - UID length
  >  - Protocol
  - Compare a digital NFC card against a digital NFC card checking for:
  >  - UID
  >  - UID length
  >  - Protocol
  >  - NFC data
- Finder Menu:
  - Physical NFC card finder:
  > This features searches your saved NFC cards searching for a match for the physical card you are scanning then displaying the path to the saved NFC card that it finds
  - Digital NFC card finder:
  > This features searches your saved NFC cards searching for a match for the digital NFC card you have chosen ignoring the one you have chosen to compare against then displaying the path to the saved NFC card that it finds
  - Settings:
  > Recursive search (enabled by default) this will search all subfolders in the NFC folder for matches