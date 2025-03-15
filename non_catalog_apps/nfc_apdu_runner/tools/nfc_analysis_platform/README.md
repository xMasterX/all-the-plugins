# NFC Analysis Platform

NFC Analysis Platform is a comprehensive tool for analyzing NFC data from Flipper Zero NFC applications. This platform provides various tools for NFC data analysis, including:

- NFC APDU Runner Response Decoder (nard)
- TLV Parser (tlv)

## Installation

### Prerequisites

- Go 1.21 or higher
- Make (optional, for simplifying the build process)

### Dependencies

```bash
go get github.com/spf13/cobra
go get github.com/fatih/color
go get go.bug.st/serial
```

### Build and Install with Make

```bash
# Initialize the project
make init

# Build the project
make build

# Install to ~/bin directory
make install
```

### Cross-Platform Building

```bash
# Build for Linux
make build-linux

# Build for Windows
make build-windows

# Build for macOS
make build-macos

# Build for all platforms
make build-all
```

### Manual Build and Install

```bash
# Initialize Go modules
go mod tidy

# Build the project
go build -o nfc_analysis_platform .

# Install to ~/bin directory
mkdir -p ~/bin
cp nfc_analysis_platform ~/bin/
mkdir -p ~/bin/format
cp -r format/* ~/bin/format/ 2>/dev/null || :
```

Make sure the `~/bin` directory is in your PATH environment variable.

## Usage

### NFC APDU Runner Response Decoder (nard)

The NFC APDU Runner Response Decoder is used to parse and display `.apdures` files from the Flipper Zero NFC APDU Runner application.

```bash
# Load .apdures file from a local file
nfc_analysis_platform nard --file path/to/file.apdures

# Load .apdures file from Flipper Zero device
nfc_analysis_platform nard --device

# Load .apdures file from Flipper Zero device using serial communication
nfc_analysis_platform nard --device --serial

# Specify serial port
nfc_analysis_platform nard --device --serial --port /dev/ttyACM0

# Specify decode format template
nfc_analysis_platform nard --decode-format path/to/format.apdufmt

# Specify format directory
nfc_analysis_platform nard --format-dir /path/to/format/directory

# Enable debug mode
nfc_analysis_platform nard --debug
```

#### Format Template Syntax

NFC APDU Runner Response Decoder uses Go templates for formatting. The following functions are available:

- `TAG(tag)`: Extract the value of a TLV tag from the response data
- `hex(data)`: Convert hexadecimal string to ASCII
- `h2d(data)`: Convert hexadecimal string to decimal
- Mathematical operations: Addition (`+`), Subtraction (`-`), Multiplication (`*`), Division (`/`)
- Slicing: `expression[start:end]` to extract a substring from the result of an expression

Example format template:
```
EMV Card Information
Application Label: {O[1]TAG(50), "ascii"}
Card Number: {O[3]TAG(9F6B)[0:16], "numeric"}
Expiry Date: Year: 20{O[3]TAG(9F6B)[17:19], "numeric"}, Month: {O[3]TAG(9F6B)[19:21], "numeric"}
Country Code: {O[1]TAG(9F6E)[0:4]}
Application Priority: {O[1]TAG(87), "hex"}
Application Interchange Profile: {O[2]TAG(82), "hex"}
PIN Try Counter: {O[4]TAG(9F17), "numeric"}
```

### TLV Parser (tlv)

The TLV Parser is used to parse TLV (Tag-Length-Value) data and extract values for specified tags.

```bash
# Parse all tags
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264

# Parse specific tags
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 84,50

# Specify data type
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 50 --type ascii
```

#### Supported Data Types

- `utf8`, `utf-8`: UTF-8 encoding
- `ascii`: ASCII encoding
- `numeric`: Numeric encoding (such as BCD code)

## Features

### NFC APDU Runner Response Decoder (nard)

- Support for custom decoding format templates
- Support for loading .apdures files from local files or Flipper Zero device
- Support for serial communication
- Debug mode to display detailed error messages
- Hexadecimal to decimal conversion
- Mathematical operations
- Slicing function results

### TLV Parser (tlv)

- Support for parsing TLV data
- Support for recursively parsing nested TLV structures
- Support for extracting values for specific tags
- Support for multiple data type displays (hexadecimal, ASCII, UTF-8, numeric)
- Colored output for improved readability

## Format Template Examples

The platform includes several example format templates in the `format` directory:

- `emv_card.apdufmt`: For decoding EMV card information

You can create your own format templates and place them in the format directory.

## Error Handling

The tool automatically detects error responses (non-9000 status codes) and handles them appropriately:

- When an output has a non-success status code (not 9000), any template expression referencing that output will return an empty string
- Error information is collected during parsing but not displayed by default
- To view error information, use the `--debug` flag when running the command
- In debug mode, error messages are displayed at the end of the output

## Contributing

Contributions are welcome, whether it's code contributions, bug reports, or suggestions for improvements. Please submit your contributions through GitHub Issues or Pull Requests.

## License

This project is licensed under the MIT License. See the LICENSE file for details. 