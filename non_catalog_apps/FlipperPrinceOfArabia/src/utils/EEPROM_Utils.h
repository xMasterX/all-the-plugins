#pragma once

#include "Arduboy2Ext.h"
#include "../entities/Cookie.h"

#define EEPROM_START                  ((uint8_t *)EEPROM_STORAGE_SPACE_START + 50)
#define EEPROM_START_C1               ((uint8_t *)EEPROM_START)
#define EEPROM_START_C2               ((uint8_t *)EEPROM_START + 1)
#define EEPROM_TOP_START              ((uint8_t *)EEPROM_START + 2)


class EEPROM_Utils {

  public: 

    EEPROM_Utils(){};
        
    static void loadCookie(Cookie &cookie);
    static void saveCookie(Cookie &cookie);

};


/* ---------------------------------------------------------------------------- */

const uint8_t letter1 = 80; 
const uint8_t letter2 = 81; 

void EEPROM_Utils::saveCookie(Cookie &cookie) {

    eeprom_update_byte(EEPROM_START_C1, letter1);
    eeprom_update_byte(EEPROM_START_C2, letter2);
    eeprom_write_block(&cookie, EEPROM_TOP_START, sizeof(cookie));

}

void EEPROM_Utils::loadCookie(Cookie &cookie) {

    byte c1 = eeprom_read_byte(EEPROM_START_C1);
    byte c2 = eeprom_read_byte(EEPROM_START_C2);

    if (c1 != letter1 || c2 != letter2) {

        cookie.hasSavedLevel = false;
        cookie.hasSavedScore = false;

    }
    else {

        eeprom_read_block(&cookie, EEPROM_TOP_START, sizeof(cookie));

    }

}
