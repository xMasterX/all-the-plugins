#pragma once

#include <stdint.h>
#include <stdbool.h>

// 24C02 EEPROM I2C addresses (7-bit)
// Standard addresses: 0x50-0x57 (A0-A2 pins)
#define EEPROM_24C02_BASE_ADDR 0x50
#define EEPROM_24C02_MAX_ADDR  0x57

// Memory size for 24C02
#define EEPROM_24C02_SIZE 256  // 2KB = 2048 bits = 256 bytes
#define EEPROM_24C02_PAGE_SIZE 8  // Page write size

// I2C operation timeout
#define EEPROM_I2C_TIMEOUT 100

class EEPROM24C02 {
private:
    uint8_t _i2c_addr_8bit;
    
public:
    EEPROM24C02(uint8_t i2c_address_7bit);
    
    // Initialize communication with EEPROM
    bool init();
    
    // Read single byte from address
    bool readByte(uint8_t memory_addr, uint8_t& data);
    
    // Write single byte to address
    bool writeByte(uint8_t memory_addr, uint8_t data);
    
    // Read multiple bytes (sequential read)
    bool readBytes(uint8_t start_addr, uint8_t* buffer, uint8_t length);
    
    // Write multiple bytes (page write)
    bool writeBytes(uint8_t start_addr, const uint8_t* buffer, uint8_t length);
    
    // Erase entire memory (fill with 0xFF)
    bool eraseAll();
    
    // Erase range of bytes
    bool eraseRange(uint8_t start_addr, uint8_t length);
    
    // Check if EEPROM is responding
    bool isAvailable();
    
    // Set I2C address
    void setAddress(uint8_t i2c_address_7bit);
    
    // Get current I2C address
    uint8_t getAddress();
};
