#include <lib/Arduino.h>
#include <lib/Sprites.h>
#include <lib/Print.h>
#include "Font3x5.h"

#define USE_LOWER_CASE

#define FONT3X5_WIDTH 3
#define FONT3X5_HEIGHT 6

#define CHAR_EXCLAMATION 33
#define CHAR_PERIOD 46
#define CHAR_LETTER_A 65
#define CHAR_LETTER_Z 90
#define CHAR_LETTER_A_LOWER 97
#define CHAR_LETTER_Z_LOWER 122
#define CHAR_NUMBER_0 48
#define CHAR_NUMBER_9 57

#ifdef USE_LOWER_CASE
    #define FONT_EXCLAMATION_INDEX 62
    #define FONT_PERIOD_INDEX 63
    #define FONT_NUMBER_INDEX 52
#else
    #define FONT_EXCLAMATION_INDEX 36
    #define FONT_PERIOD_INDEX 37
    #define FONT_NUMBER_INDEX 26
#endif

const uint8_t PROGMEM font_images[] = {
    3, 8,

    // #65 Letter 'A'.
    0x1F,  // ░░░▓▓▓▓▓
    0x05,  // ░░░░░▓░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #66 Letter 'B'.
    0x1F,  // ░░░▓▓▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1B,  // ░░░▓▓░▓▓

    // #67 Letter 'C'.
    0x1F,  // ░░░▓▓▓▓▓
    0x11,  // ░░░▓░░░▓
    0x11,  // ░░░▓░░░▓

    // #68 Letter 'D'.
    0x1F,  // ░░░▓▓▓▓▓
    0x11,  // ░░░▓░░░▓
    0x0E,  // ░░░░▓▓▓░

    // #69 Letter 'E'.
    0x1F,  // ░░░▓▓▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x11,  // ░░░▓░░░▓

    // #70 Letter 'F'.
    0x1F,  // ░░░▓▓▓▓▓
    0x05,  // ░░░░░▓░▓
    0x01,  // ░░░░░░░▓

    // #71 Letter 'G'.
    0x1F,  // ░░░▓▓▓▓▓
    0x11,  // ░░░▓░░░▓
    0x1D,  // ░░░▓▓▓░▓

    // #72 Letter 'H'.
    0x1F,  // ░░░▓▓▓▓▓
    0x04,  // ░░░░░▓░░
    0x1F,  // ░░░▓▓▓▓▓

    // #73 Letter 'I'.
    0x00,  // ░░░░░░░░
    0x1F,  // ░░░▓▓▓▓▓
    0x00,  // ░░░░░░░░

    // #74 Letter 'J'.
    0x10,  // ░░░▓░░░░
    0x10,  // ░░░▓░░░░
    0x1F,  // ░░░▓▓▓▓▓

    // #75 Letter 'K'.
    0x1F,  // ░░░▓▓▓▓▓
    0x04,  // ░░░░░▓░░
    0x1B,  // ░░░▓▓░▓▓

    // #76 Letter 'L'.
    0x1F,  // ░░░▓▓▓▓▓
    0x10,  // ░░░▓░░░░
    0x10,  // ░░░▓░░░░

    // #77 Letter 'M'.
    0x1F,  // ░░░▓▓▓▓▓
    0x06,  // ░░░░░▓▓░
    0x1F,  // ░░░▓▓▓▓▓

    // #78 Letter 'N'.
    0x1F,  // ░░░▓▓▓▓▓
    0x01,  // ░░░░░░░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #79 Letter 'O'.
    0x1F,  // ░░░▓▓▓▓▓
    0x11,  // ░░░▓░░░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #80 Letter 'P'.
    0x1F,  // ░░░▓▓▓▓▓
    0x05,  // ░░░░░▓░▓
    0x07,  // ░░░░░▓▓▓

    // #81 Letter 'Q'.
    0x1F,  // ░░░▓▓▓▓▓
    0x31,  // ░░▓▓░░░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #82 Letter 'R'.
    0x1F,  // ░░░▓▓▓▓▓
    0x05,  // ░░░░░▓░▓
    0x1B,  // ░░░▓▓░▓▓

    // #83 Letter 'S'.
    0x17,  // ░░░▓░▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1D,  // ░░░▓▓▓░▓

    // #84 Letter 'T'.
    0x01,  // ░░░░░░░▓
    0x1F,  // ░░░▓▓▓▓▓
    0x01,  // ░░░░░░░▓

    // #85 Letter 'U'.
    0x1F,  // ░░░▓▓▓▓▓
    0x10,  // ░░░▓░░░░
    0x1F,  // ░░░▓▓▓▓▓

    // #86 Letter 'V'.
    0x0F,  // ░░░░▓▓▓▓
    0x10,  // ░░░▓░░░░
    0x0F,  // ░░░░▓▓▓▓

    // #87 Letter 'W'.
    0x1F,  // ░░░▓▓▓▓▓
    0x0C,  // ░░░░▓▓░░
    0x1F,  // ░░░▓▓▓▓▓

    // #88 Letter 'X'.
    0x1B,  // ░░░▓▓░▓▓
    0x04,  // ░░░░░▓░░
    0x1B,  // ░░░▓▓░▓▓

    // #89 Letter 'Y'.
    0x07,  // ░░░░░▓▓▓
    0x1C,  // ░░░▓▓▓░░
    0x07,  // ░░░░░▓▓▓

    // #90 Letter 'Z'.
    0x19,  // ░░░▓▓░░▓
    0x15,  // ░░░▓░▓░▓
    0x13,  // ░░░▓░░▓▓

    #ifdef USE_LOWER_CASE

        // #97 Letter 'a'.
        0x0C,  // ░░░░▓▓░░
        0x12,  // ░░░▓░░▓░
        0x1E,  // ░░░▓▓▓▓░

        // #98 Letter 'b'.
        0x1F,  // ░░░▓▓▓▓▓
        0x12,  // ░░░▓░░▓░
        0x0C,  // ░░░░▓▓░░

        // #99 Letter 'c'.
        0x1E,  // ░░░▓▓▓▓░
        0x12,  // ░░░▓░░▓░
        0x12,  // ░░░▓░░▓░

        // #100 Letter 'd'.
        0x0C,  // ░░░░▓▓░░
        0x12,  // ░░░▓░░▓░
        0x1F,  // ░░░▓▓▓▓▓

        // #101 Letter 'e'.
        0x0C,  // ░░░░▓▓░░
        0x1A,  // ░░░▓▓░▓░
        0x14,  // ░░░▓░▓░░

        // #102 Letter 'f'.
        0x04,  // ░░░░░▓░░
        0x1F,  // ░░░▓▓▓▓▓
        0x05,  // ░░░░░▓░▓

        // #103 Letter 'g'.
        0x2E,  // ░░▓░▓▓▓░
        0x2A,  // ░░▓░▓░▓░
        0x1E,  // ░░░▓▓▓▓░

        // #104 Letter 'h'.
        0x1F,  // ░░░▓▓▓▓▓
        0x02,  // ░░░░░░▓░
        0x1C,  // ░░░▓▓▓░░

        // #105 Letter 'i'.
        0x00,  // ░░░░░░░░
        0x1D,  // ░░░▓▓▓░▓
        0x00,  // ░░░░░░░░

        // #106 Letter 'j'.
        0x20,  // ░░▓░░░░░
        0x1D,  // ░░░▓▓▓░▓
        0x00,  // ░░░░░░░░

        // #107 Letter 'k'.
        0x1F,  // ░░░▓▓▓▓▓
        0x04,  // ░░░░░▓░░
        0x1A,  // ░░░▓▓░▓░

        // #108 Letter 'l'.
        0x01,  // ░░░░░░░▓
        0x1F,  // ░░░▓▓▓▓▓
        0x00,  // ░░░░░░░░

        // #109 Letter 'm'.
        0x1E,  // ░░░▓▓▓▓░
        0x04,  // ░░░░░▓░░
        0x1E,  // ░░░▓▓▓▓░

        // #110 Letter 'n'.
        0x1E,  // ░░░▓▓▓▓░
        0x02,  // ░░░░░░▓░
        0x1E,  // ░░░▓▓▓▓░

        // #111 Letter 'o'.
        0x1E,  // ░░░▓▓▓▓░
        0x12,  // ░░░▓░░▓░
        0x1E,  // ░░░▓▓▓▓░

        // #112 Letter 'p'.
        0x3E,  // ░░▓▓▓▓▓░
        0x12,  // ░░░▓░░▓░
        0x0C,  // ░░░░▓▓░░

        // #113 Letter 'q'.
        0x0C,  // ░░░░▓▓░░
        0x12,  // ░░░▓░░▓░
        0x3E,  // ░░▓▓▓▓▓░

        // #114 Letter 'r'.
        0x1E,  // ░░░▓▓▓▓░
        0x02,  // ░░░░░░▓░
        0x06,  // ░░░░░▓▓░

        // #115 Letter 's'.
        0x14,  // ░░░▓░▓░░
        0x12,  // ░░░▓░░▓░
        0x0A,  // ░░░░▓░▓░

        // #116 Letter 't'.
        0x02,  // ░░░░░░▓░
        0x0F,  // ░░░░▓▓▓▓
        0x12,  // ░░░▓░░▓░

        // #117 Letter 'u'.
        0x1E,  // ░░░▓▓▓▓░
        0x10,  // ░░░▓░░░░
        0x1E,  // ░░░▓▓▓▓░

        // #118 Letter 'v'.
        0x0E,  // ░░░░▓▓▓░
        0x10,  // ░░░▓░░░░
        0x0E,  // ░░░░▓▓▓░

        // #119 Letter 'w'.
        0x1E,  // ░░░▓▓▓▓░
        0x08,  // ░░░░▓░░░
        0x1E,  // ░░░▓▓▓▓░

        // #120 Letter 'x'.
        0x1A,  // ░░░▓▓░▓░
        0x04,  // ░░░░░▓░░
        0x1A,  // ░░░▓▓░▓░

        // #121 Letter 'y'.
        0x2E,  // ░░▓░▓▓▓░
        0x28,  // ░░▓░▓░░░
        0x1E,  // ░░░▓▓▓▓░

        // #122 Letter 'z'.
        0x1A,  // ░░░▓▓░▓░
        0x12,  // ░░░▓░░▓░
        0x16,  // ░░░▓░▓▓░

    #endif

    // #48 Number '0'.
    0x1F,  // ░░░▓▓▓▓▓
    0x11,  // ░░░▓░░░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #49 Number '1'.
    0x00,  // ░░░░░░░░
    0x1F,  // ░░░▓▓▓▓▓
    0x00,  // ░░░░░░░░

    // #50 Number '2'.
    0x1D,  // ░░░▓▓▓░▓
    0x15,  // ░░░▓░▓░▓
    0x17,  // ░░░▓░▓▓▓

    // #51 Number '3'.
    0x11,  // ░░░▓░░░▓
    0x15,  // ░░░▓░▓░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #52 Number '4'.
    0x07,  // ░░░░░▓▓▓
    0x04,  // ░░░░░▓░░
    0x1F,  // ░░░▓▓▓▓▓

    // #53 Number '5'.
    0x17,  // ░░░▓░▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1D,  // ░░░▓▓▓░▓

    // #54 Number '6'.
    0x1F,  // ░░░▓▓▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1D,  // ░░░▓▓▓░▓

    // #55 Number '7'.
    0x01,  // ░░░░░░░▓
    0x01,  // ░░░░░░░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #56 Number '8'.
    0x1F,  // ░░░▓▓▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #57 Number '9'.
    0x17,  // ░░░▓░▓▓▓
    0x15,  // ░░░▓░▓░▓
    0x1F,  // ░░░▓▓▓▓▓

    // #33 Symbol '!'.
    0x00,  // ░░░░░░░░
    0x17,  // ░░░▓░▓▓▓
    0x00,  // ░░░░░░░░

    // #46 Symbol '.'.
    0x00,  // ░░░░░░░░
    0x10,  // ░░░▓░░░░
    0x00   // ░░░░░░░░

};


Font3x5::Font3x5(uint8_t lineSpacing) {

    _lineHeight = lineSpacing;
    _letterSpacing = 1;

    _cursorX = _cursorY = _baseX = 0;
    _textColor = 1;

}

size_t Font3x5::write(uint8_t c) {

    if (c == '\n')      { _cursorX = _baseX; _cursorY += _lineHeight; }
    else {

        printChar(c, _cursorX, _cursorY);
        _cursorX += FONT3X5_WIDTH + _letterSpacing;

    }

    return 1;

}

void Font3x5::printChar(const char c, const int8_t x, int8_t y) {

    int8_t idx = -1;

    ++y;

    switch (c) {

        case CHAR_LETTER_A ... CHAR_LETTER_Z:
            idx = c - CHAR_LETTER_A;
            break;

            #ifdef USE_LOWER_CASE    
                case CHAR_LETTER_A_LOWER ... CHAR_LETTER_Z_LOWER:
                idx = c - CHAR_LETTER_A_LOWER + 26;
                break;
            #endif

        case CHAR_NUMBER_0 ... CHAR_NUMBER_9:
            idx = c - CHAR_NUMBER_0 + FONT_NUMBER_INDEX;
            break;

        case CHAR_EXCLAMATION:
            idx = FONT_EXCLAMATION_INDEX;
            break;

        case CHAR_PERIOD:
            idx = FONT_PERIOD_INDEX;
            break;

    }

    if (idx > -1) {

        if (_textColor == WHITE) {
            Sprites::drawSelfMasked(x, y, font_images, idx);
        }
        else {
            Sprites::drawErase(x, y, font_images, idx);
        }

    }

}

void Font3x5::setCursor(const int8_t x, const int8_t y) {
    _cursorX = _baseX = x;
    _cursorY = y;
    }

void Font3x5::setTextColor(const uint8_t color){
    _textColor = color; 
}

void Font3x5::setHeight(const uint8_t color){
    _lineHeight = color;
}
