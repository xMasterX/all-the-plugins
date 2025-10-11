NFC APDU Runner is a Flipper Zero application for reading and executing APDU commands on NFC cards. This application allows users to load APDU commands from script files, send them to NFC cards, and view the response results. It also includes NARD (NFC APDU Runner Response Decoder) for parsing and analyzing APDU responses using customizable format templates, and TLV data extraction capabilities. The project now supports a Web interface analysis platform for more comprehensive NFC data analysis and visualization.

## Features

- Support for loading APDU commands from script files
- Web interface analysis platform for comprehensive NFC data analysis and visualization
- Support for multiple card types (ISO14443-4A and ISO14443-4B implemented)
- User-friendly interface with operation prompts
- Execution logging for debugging
- Ability to save execution results to files
- NARD (NFC APDU Runner Response Decoder) for parsing and analyzing APDU responses
- Template-based decoding of APDU responses with custom format templates
- TLV data extraction and parsing capabilities

## Supported Card Types

- ISO14443-4A (implemented)
- ISO14443-4B (implemented)
- ISO14443-3A (APDU commands not supported)
- ISO14443-3B (APDU commands not supported)

## Usage

1. Launch the NFC APDU Runner application on Flipper Zero
2. Select "Load Script" to load an APDU script file
3. Place the NFC card on the back of the Flipper Zero
4. Click "Run" to execute APDU commands
5. View the execution results
6. Choose to save or discard the results

## Development Information

- Author: SpenserCai
- Version: 0.3
- License: GNU General Public License v3.0

## Notes

- The application requires NFC cards that support APDU commands
- ISO14443-3A and ISO14443-3B cards do not support APDU commands
- Ensure the card is placed correctly so that Flipper Zero can read it

## Troubleshooting

If you encounter issues:
1. Make sure the card type matches the type specified in the script file
2. Ensure the card is placed correctly
3. Check if the APDU command format is correct
4. View the execution logs for detailed information

## License

This project is licensed under the GNU General Public License v3.0 - see the LICENSE file for details. 

[See full readme here](https://github.com/SpenserCai/nfc_apdu_runner/blob/main/README.md)