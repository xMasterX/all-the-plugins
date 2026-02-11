# Python utilities for Arduboy

For Windows, Linux and OSX

### Dependencies

* Requires Python 3 with PySerial and Pillow installed.

### Installing dependencies

* Download and install python 3 from https://www.python.org/downloads/
* Make sure the option 'Add python.exe to path' is checked on install options (Windows)
* After install run 'python -m pip install pyserial' from command line. For OSX run 'easy_install pyserial' from terminal.
* Also run 'python -m pip install pillow' from command line to install the pillow module. For OSX run 'easy_install pillow' from terminal.

## Uploader
* Requires pySerial: `python -m pip install pyserial`

.Hex file and .Arduboy uploader for Arduboy

* Double click the **uploader-create-send-to-shortcut.vbs** for right click Send to upload option(Windows only)

### Features

* Supports uploading to Arduboy, DevKit, and homemade Arduboys
* Uploads .hex files, .hex files in .zip and .arduboy files
* Protects unprotected bootloaders from being overwritten by large hex files
* Supports on the fly patching for SSD1309 displays
* Supports on the fly RX and TX LED polarity patching for Arduino /Genuino Micro

### Usage:

* Right click a .hex file, .arduboy file or .zip file containing a hex file
  and choose *Send To* Arduboy uploader
* Drag and drop .hex, .zip or .arduboy files on the **uploader.py** file
* Command line: uploader.py [filetoupload]

## SSD1309 display support

To patch Arduboy hex files for use on Homemade Arduboys with SSD1309 displays,
make a copy of **uploader.py** and rename it to **uploader-1309.py** Also make
sure you run the **uploader-create-send-to-shortcut.vbs** again to create a
**Send To** shortcut for it. Files will be patched on fly, original files will not be altered.

## reverse RX and TX LED polarity support

To patch Arduboy hex files for use with Homemade Arduboys based on Arduino / Genuino Micro,
make a copy of **uploader.py** and rename it to **uploader-micro.py** and run the
**uploader-create-send-to-shortcut.vbs** again to create a **Send To** shortcut for it.

## EEPROM backup

You can backup your Arduboys EEPROM by double clicking the **eeprom-backup.py**  python script.
The backup is saved to a time stamped file in the format **eeprom-backup.py-YYYYMMDD-HHMMSS.bin**

## EEPROM restore

You can restore a previously made EEPROM backup simply by dragging the **eeprom-backup.py-YYYYMMDD-HHMMSS.bin** file onto the **eeprom-restore.py** python script

## EEPROM erase

Erases the EEPROM content (An erased EEPROM contains all 0xFF's).

## Erase sketch

Erases the application/sketch startup page to keep the bootloader mode active indefinitely. This solves problematic (time sensitive) uploads using Arduino IDE.

## Flash cart builder

* Requires PILlow: `python -m pip install pillow`

Builds a binary flash image from an index file and supporting resource files (.png images and .hex files).  Use the **flashcart-writer.py** script to write the output to a flash cart.  See the **example-flashcart\flashcart-index.csv** file for example syntax.

example: `python flashcart-builder.py example-flashcart\flashcart-index.csv`

## Flash cart writer

* Requires pySerial: `python -m pip install pyserial`

Writes a binary flash image to external flash memory of Arduboy FX and Arduboy (clones) with added serial flash memory (Cathy3K v1.3+ bootloader required). Use the **flashcart-builder.py** script to build the image.
To automatically apply the SSD1309 patch to the uploaded image, make a copy of **flashcart-writer.py** and rename it to **flashcart-writer-1309.py**.

example: `python flashcart-writer.py example-flashcart\flashcart-image.bin`

For development purposes external program data and save data can be stored at the end of external flash memory using -d and -s switches.

example: `python flashcart-writer.py -d datafile.bin`

## Flash cart backup

* Requires pySerial: `python -m pip install pyserial`

Backup your existing flash cart to a binary image that can later be re-written to the Arduboy using the **flashcart-writer.py** script.
The backup is saved to a time stamped file in the format **flashcart-backup-image-YYYYMMDD-HHMMSS.bin**

example: `python flashcart-backup.py`

## Image Converter

* Requires PILlow: `python -m pip install pillow`

Converts .bmp or .png image files to C++ include file. Image width and height can be any size. Tilesheets and spritesheets with optional spacing can be converted by
specifying the width and height and optional spacing in the filename.
When an image contains transparency information the converted data will include a sprite mask.
Script can convert multiple files in one go by supplying multiple filenames.

example: `python image-converter.py tilesheet_16x16.png`
